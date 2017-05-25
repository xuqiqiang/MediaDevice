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

        FCT_TEST_BGN(gcm_nist_validation_aes_25612800128_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "fb8094dd2eddb3d8004bb79134023ca2be4de9b668a9e4608abdf2130e8becb8" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "491a14e13b591cf2f39da96b6882b5e5" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "80883f2c925434a5edfcefd5b123d520" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612800128_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "725313f4cb3f6a0d29cefc174b7e4f43cef11b761ef75e1995cb64c1306795f1" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "27d1ed08aba23d79fc49ad8d92a2a0ea" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "d5d6637ba35ef2ad88e9725f938d3d2d" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612800128_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "4e766584ce0e885e1bba1327e5335796de0831a40f74a5cec178081dd15bfd10" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "cece0dea024ff47851af0500d146cbfe" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "1abe16eeab56bd0fb1ab909b8d528771" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612800120_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "ce7f2207f83a952451e714ba3807ddb3ed67c2739a628980411aa68366b1f2f5" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "652fd951ace288db397020687135a5d1" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "985227b14de16722987a3d34976442" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612800120_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "855f8fa4ec6a1206173509d504d0b29dfbfbfa9aa528254b189cd72e6ebc1c1f" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "1ad1507e6463e4e2e1a63155ac0e638f" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "693146a8b833f324c1d4cbeeb8c146" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612800120_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "ef8dd1294a85dd39e366f65e1076d53e046188c06c96b2c9e84ebc81f5c9f550" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "9698a07447552d1a4ecd2b4c47858f06" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "b00590cac6e398eeb3dcb98abe1912" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612800112_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "25896e587570ff1823639e1e51e9c89192d551b573dd747e7c0c1c10916ece4c" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "f0516457c09c372c358064eb6b470146" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "5a7cadec600a180e696d946425b0" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612800112_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "02fc9cfffbe72e7954182993088e09d24ea8cad91a8ca9a336d9f1fe4156486d" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "0e189e162e097eb2060b30c46d9afa70" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "7d3d5cc55e6182ec5413ef622d4f" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612800112_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "f24e3d631d8961d3d4b9912d4fa7a317db837a7b81cd52f90c703a4835c632e2" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "510740bfa2562ce99ca3839229145a46" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "1402ddc1854e5adb33664be85ad1" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612800104_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "720ab5aceb80ff1f864379add9b0d63607227f7c3f58425dd6ec3d4cea3fe2ea" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "58f2317afb64d894243c192ef5191300" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "e8e772402cc6bfd96a140b24c1" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612800104_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "f57dd16fa92a8f8c09d8f13cb5b6633a43b8762e90c670232f55949cdfdf700c" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "3b7c14ee357b3c6b0dc09e3209ab69f2" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "43e609664e48ad1f5478087f24" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612800104_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "87c17ab919a4bc0d50343c0bb282a969283c2ada25f9a96d2858c7f89bc5139a" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "02813d3faf30d3e186d119e89fe36574" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "d1a1f82a8462c783b15c92b57e" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_2561280096_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "dd8d5b6c5c938c905c17eab9f5ab7cd68d27f3f09d75177119010d070b91e646" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "1df1c3ad363c973bffe29975574ffdf6" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "749ac7ffda825fc973475b83" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_2561280096_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "4d60a14cb789099c77b8991e7b0b40f787d3458f448501e8108e4d76110f94ef" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "ca6b3485eb5dcd9dbfa7cffcdb22daa5" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "3f868b6510d64098adc1d640" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_2561280096_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "405b690717de993ad945d80159c2800848060de0b7d2b277efd0350a99ba609a" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "63730acb957869f0c091f22d964cc6a3" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "739688362337d61dab2591f0" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_2561280064_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "ab5563a387e72d7d10468c99df590e1de25ec10363aa90d1448a9ffcd1de6867" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "c511406701bad20a2fa29b1e76924d2f" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "390291ed142ba760" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_2561280064_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "abef7c24daaa21f308a5af03df936ba3f70aa525190af0d959d6e50d836f4624" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "e9f15950130b9524e2b09f77be39109a" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "db2fb2b004bc8dc4" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_2561280064_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "6ca630b0b6779a8de7a19e5279eac94bf29f76f8b0cf8ecf8f11c4f8eb04aa0d" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "7373befc2c8007f42eef47be1086842f" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "e2b8620bcc7472a8" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_2561280032_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "acea7818a71df2c9840aef1c10ecbe2bac7e92216388416a2f36119a0745d883" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "6d46aa39fb5a6117e9adf7ee72bc50ff" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "fd5ff17b" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_2561280032_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "b301036d4b2b28b8a4502925986861eba2b67c24cb0c79c63fd62195d9b67506" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "bb6f398e5aed51590e3df02f5419e44d" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "47f3a906" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_2561280032_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "89576d2aac554c8982c7df0053be9ab19f4bd80ba9f3dd433c1c054d68e68795" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "aedbd482a401a7c12d4755077c8dd26e" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "506fa18d" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612801024128_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "43c9e209da3c1971d986a45b92f2fa0d2d155183730d21d71ed8e2284ec308e3" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "78bef655dfd8990b04d2a25678d7086d" );
            add_len = unhexify( add_str, "9d8c6734546797c581b9b1d0d4f05b27fe0539bd01655d2d1a8a1489cdf804228753d77272bf6ded19d47a6abd6281ea9591d4bcc1be222305fdf689c5faa4c11331cffbf42215469b81f61b40415d81cc37161e5c0258a67642b9b8ac627d6e39f43e485e1ff522ac742a07defa3569aeb59990cb44c4f3d952f8119ff1111d" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "f15ddf938bbf52c2977adabaf4120de8" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612801024128_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "fbe2d52b7f50bf23a16ff8cd864215034fdfbf4d1506ca3c1ffb015653efe33a" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "b155f8ab1a8c0327789cfb8310051f19" );
            add_len = unhexify( add_str, "ed8d14adf1c362bbaf0d569c8083278e8225f883d75d237a4abcd775a49780603e50c00a1b5b5946c085e57a749b4946f6aca96eda04ac9944a7d3d47adc88326ed30a34d879dd02fb88182f9e2deefaeee1c306b897539fa9075bda03ba07b4ffff71ce732ef3c4befac0f18c85a0652d34524ccb1a4747ab8f72ed1c24d8fc" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "c5fe27ca90e5c8b321cc391ee7f1f796" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612801024128_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "8e888721514fd01fb67513cb56bfd29af67a9ce525e3e697af47450f02053161" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "9f6bd4a93e4f3f2f5f4a7c2c5b4790bf" );
            add_len = unhexify( add_str, "867d50923967535ce6f00395930083523c22f373cfb6c8817764f5623cd60b555572404e54f2fe7083ef32b9a4593a1f70a736d6e8fe61b77def51f3b1d8f679d3a8d50d0aad49e51ec1eb4d4a25f13d14f3e5253555c73eac759e484c6131cc868b46c18b26acd040c3e1cb27afecba7b7fc3f5ff4883f4eafc26c7f3084751" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "ea269094330b6926627889fcdb06aab4" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612801024120_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "d8f82b07e7319ca607c9aa0352070ca883dd7b32af370a774f63b0270f44835a" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "e89e4484497cb728f86585d8918b7fae" );
            add_len = unhexify( add_str, "42340d96e1852de3ed5e30eb4a05e1fb222480b450e2bf4e2cf0fb2a525eb6602ef43a896adc5c52ea5381c642b2175691c014e7a6dae91fa6ff5b95c18a2dd2e8838d3abd46ace0b305f3f22d30a0bd82a81bbf6753362b54b0624c76c0d753e30eb636365f0df7e1bf8bf130cf36062ec23f58a3f7ed0ae7bfbbd68460cd76" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "b234b28917372374e7f304f1462b49" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612801024120_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "b49b04a54a08d28b077ea54c18bfa53e916723e91453b47f88e399046b9b4dcc" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "6276c577c530f91b434ce5719e1c59de" );
            add_len = unhexify( add_str, "6b73f996c49e368fc4d21816153aefb081509f9dc0916dbe4fdf77f39867a2bd617b8a75f39f515b1bc1454009d5247efcd90ba0d4a6743c6f12a929b666584f3b55254c32e2bab2321f94fa843dc5124c341dd509788a158191ee141eb0bc4e1b96f6987bafe664a0f9ac6d85c59cee9564a27bcc37dffae80c57fbf7e748ce" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "69dd5bdeb15fdbc3a70c44b150f70e" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612801024120_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "398bb37bb991898c7dad7bf5930dbad20d121f68d5ec6c56ffe66f23c0c37f8e" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "0c3bd55b54c1221b0cf25d88ea4dfe24" );
            add_len = unhexify( add_str, "4c48b929f31180e697ea6199cd96c47cecc95c9ed4c442d6a23ca3a23d4b4833601ac4bbcdbc333cd1b3a0cd90338e1c88ef8561fed7ad0f4f54120b76281958995c95e4c9daabff75d71e2d5770420211c341c6b062b6c8b31b8fe8990588fbad1e651a49b0badd9a8d8042206337a1f2aa980b3ba3b5ee8e3396a2b9150a34" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "8528950bd5371681a78176ae1ea5dc" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612801024112_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "8e8f7c317b22dea8eabe7eaa87413a98ff56570720985b6743a5f9af56387cca" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "3a9a5a839045723afdfb2d5df968bfcb" );
            add_len = unhexify( add_str, "a87d95f8f47e45a1c7c5c58d16055b52b3256c52713fd092bcd6cbc44e2c84669f23ca2a19e34163ee297f592f6054dbc88863a896c2217e93a660d55a6cd9588a7275d05649940d96815c7ddfa5fc4394c75349f05f1bcaff804095783726c0eceb79833a48cefd346b223f4e5401789684e5caeda187a323962a1f32f63f02" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "faad6a9731430e148ace27214e68" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612801024112_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "67c95e57197f0e0bbaaa866d337fcc37f3a10dc55a059f5ea498de204d2fff61" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "5f171d203c653a316cac43df99f4033a" );
            add_len = unhexify( add_str, "84f281b388ca18bc97323657a723a56260731234720b02b6dde00ea134bd84a1893bec38af80214c4da01b93958ab00f3b648c975371e565d5b6bf2a8f63c0f3cfcd557c9f63574390b6ae533085aca51fa9d46cd2478b7648b6dcbbac7e61197a425778debe351ac2110ba510a17e2c351ba75d5a755ef547cf9acc54650222" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "9ea9c716e06a274d15a3595a0c41" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612801024112_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "9143f00e31c72bd9fced31585d047f67f1004e6244c3d9c10c8ae005feeabc84" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "e49cd6af9a2f0da2a7198317da92ab2f" );
            add_len = unhexify( add_str, "ab9193a155140d265aabfe2dd5efca7d3fa6129498532bccd77f09fa1a480702620b3ab53df91b01262122f1a6fc387b5fc55dadfcdb99ada83d4a5b0666c8526de309f41eb54d69b52595c43550a6bf7b4b8f0e0c48311b521762eaa567744c4c4704dd977f84068b59db98a67e33cc65302ba59360d600a22138c5ad3317f3" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "8293e361fe0308a067f89aea393f" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612801024104_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "d0ba180075c373116bb037907b512add00ba9a4693a8ecc14ca0d79adada90e3" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "5c1501b19cce5404dccc9217ac8253b7" );
            add_len = unhexify( add_str, "3a161605ec0055c479dd48cdaeed5981b8b60fb7b7781cc4e580218c7014c3060a9f706e6e16cf4021e4d38deb512534b484ff23b701975bdf901146ccaece9c3ffbbeeb172cfb64a915ae0dbe7a082b9077776a387b58559a881b9b79b90aa28ad1ac0f2bece314169a2f79ea4c08389f7f7dd10ee2d9a844fee79e7bf38bcf" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "0541262fddfd5d01ff0f3c2fb4" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612801024104_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "c975c7e59133c231d1b84c696761c413ba20aff7fb7d854c6947e65db3cc57b4" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "d8fedda4cccaf6b0818edcfa7b1f03fa" );
            add_len = unhexify( add_str, "cb4cc9171367d6422abfaf2b4452da267eb9ccf1c4c97d21a0a125de486997832d16c7e412cb109eb9ac90c81dfe1a1dd9f79af7a14e91669b47f94e07d4e9bd645d9daa703b493179ca05ddd45433def98cf499ff11849cc88b58befbdd388728632469d8b28df4451fc671f4a3d69526a80c2e53e4fdee6300d27d97baf5f4" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "77ac205d959ec10ae8cee13eed" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612801024104_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "a86ec688222c50c07274ed2d2c8ae6f883e25f8f95d404a7538fd83224199327" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "99c73fdb8f97f225f7a17cf79c011112" );
            add_len = unhexify( add_str, "cf5f707de0357262c0997fa3ebfe6e07192df8db5f029e418989e85e6b71e186b00c612ecedbfe3c847e58081847f39697337ae7c815d2cd0263986d06bf3a5d2db4e986dbe69071fd4b80a580f5a2cf734fc56c6d70202ea3494f67539797252d87cd7646296932959c99797a0446532f264d3089dd5f4bcceaaa7289a54380" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "c2093ad4705e613b09eee74057" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_2561280102496_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "d3981f0aa1ed8cb369d9b0d7b0e529ec6089ff2d226c542885b1bff55276e891" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "7331f91bd1a67c21c9dd336a2a922839" );
            add_len = unhexify( add_str, "406d9cf45fc8618d564154241dc9c006ecdcd847406e5a6e7127ac96e7bb93f4c339ff612c514b6f66df95a0845035d7535212a2aaeeb0ee512d1f4375c9a527e4e499389c2d7f7f7439c913ea91580e7303767b989c4d619df7888baf789efd489b08eda223f27da5e177cd704c638f5fc8bf1fecfcd1cab4f4adfbc9d1d8ba" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "dbb7ec852c692c9a0e1a5acd" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_2561280102496_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "8436967f97c59ca73b760b73c6e088d1da4e76b712188ab4781d8d849505ae47" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "9401dd0998914645668d06d518bfe7d7" );
            add_len = unhexify( add_str, "a5f40906177417097c19a0a21dbb457a694e173141837f695b09c8eb58ac2ce28aace4e59275b6266da9369a9905b389e968aefc64d78c7e1d2f034ef413d3458edcb955f5cd7971c28cd67dc9901ef3a2abc6121704bb5ecd87a6568d0506abbc87a2f10205dc8eb0cd1b5109158d0e743c2c3a342d60b8d55bbcb8d8507ed1" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "dd6d988d352decc4e70375d8" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_2561280102496_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "ce6b846bcedc6ae747e66e72cd9f7664e6cad9627ba5f1f1923f3d3a6ed590d1" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "ac865ff8a6255e501b347a6650510d05" );
            add_len = unhexify( add_str, "1658b9f8469af1dfa60458cf8107db1edd1e4bba70a0bd23e13e1bba0d397abf51af8348f983fcdfcc8315ef1ffc9a26371377c62ddba08363bd2bf0ff7d0c3b603fad10be24ecee97b36d2255a8b2efc63f037123cef4bb4fe384aa0c58548b2f317c36ef3ef204b24769de6ba3e9d89e159e2bf1f9d79aeb3eb80c42eb255e" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "7ee87acd138c558455fff063" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_2561280102464_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "0038ecf1407bbf0d73afa5e010769b71e8649c4249345dcf923ef9da0254c6af" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "74c6b98fc6ced3a59bd9c42d31d71095" );
            add_len = unhexify( add_str, "467f483c71c3404fe7f09d6f6b6b64c3b7613a0dd32470cf24bc590d3994a48f3e8cd5dc19ea8ca7d5366ad7c5ad31cc9612dafedaea109dde2aedfe5fc2a0db2c903dd1dc1a13949720a10babf37fba5a0ed7cb5f3dc9eb5a4d8331f218e98763e7794b3e63705d414ef332160b0b1799f1ff5cbe129a75e5c4e0a4ed35e382" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "62fe088d9129450b" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_2561280102464_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "19fc4c22151ee8515036c38bc5926c0e0bbd93db5d0fc522b2a6bf6298fed391" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "9547f056c6fb9ef72b908f527cb500c1" );
            add_len = unhexify( add_str, "511b15c25b2a324159e71c3b8e47f52d3e71e5bc35e774c39067250f4494c9c4eb184ecbe8638de9418672d9ae2c6a0e7f54c017879ffb2a371de1639693d654a43cb86e94a7350508490191790d1265b99e7b3253838b302aae33590949a8761a3bb2aeb1ba798cddeb00a53daad05a33389d4a19269d65116a84f12dba5830" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "04623912bb70810e" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_2561280102464_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "3b5d3b1920b5a105b148153ae1f1027c6d48bc99640ea853f5955fed4eb3d625" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "9a4091c2eb7e88759bd9169fee303485" );
            add_len = unhexify( add_str, "aa680d07143ba49a9099d555105fc3cfcb898cec11ade96776dc9778cc50fe972e1e83c52c837b71e27f81d1577f9bd09afe2260dfd9a5d9dfbd3b8b09a346a2ab48647f5dd2ff43700aecce7fa6f4aeea6ea01b2463c4e82ec116e4d92b309c5879fb4e2ca820d0183a2057ae4ad96f38a7d50643a835511aedd0442b290be3" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "033bfee6b228d59b" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_2561280102432_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "f6c4ad8e27764157789252f4bc4a04145cb9721955330a2f6a2a3b65cacf22bc" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "3de136cbd75061c888226efab136849d" );
            add_len = unhexify( add_str, "0f6951c127d6bc8970e2ad2799e26c7fb9ca31d223155f88374984b5660626c83276ffa6c160f75e0e1bcfa96616188f3945b15fc1b82a4e0ee44000a684b3c3840465aebe051208379ef3afe9f569ee94973d15f0a40c6f564fa4ba11d6e33cf8ae17854a9e12360a2b8495e2cceec463f5e3705c74069ba37ba6d725f458c0" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "f658c689" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_2561280102432_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "30cd99fed9706c409e366d47fefc191f79bcc47a28be78f9890fd90d4864eb85" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "8c7ce34691503bf14c776f8809f24e61" );
            add_len = unhexify( add_str, "4b6b10c2e2905ab356769b6453dd160a08e8623b0878fcc1c1d64822f0aea1f4f5b4698ded5d23ebafa11bc1e4ce9e5cd7d7c7b13de02d11a945ba8361b102ba49cdcfd6a416e3db774cd7bda024fccd1ad3087560dc15bbfe9b1a5c6c71fae17a329f104f6c2cba7eb6a7459535ca328146d0ccc0a9bd28a3d1c961947a3876" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "7777c224" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_2561280102432_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "9472f2452933dcfac4bb22831ce83c6a1ddf25ef8d2d3ba59d72b0d173a986e8" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "18fb2c34b0955d712960009617d300ef" );
            add_len = unhexify( add_str, "d283dd75cd4689c266c8e0b4b6586278aa2583c7c41bf12bd1cfdef21d349acbbabc0a2204dc4130f922949206c4fbdce3786ab8614e32908838a13b6990453abf14b84f5812e6093644accdd35f7ad611ea15aefae28b3cf1fc5da410bcea4f0a50d377fdcceffe488805bc5a71fab019b12fa8725d6e7c91e6faf12fbaf493" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "c53b16a1" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810240128_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "e06d5319210f4107ea7267fa2e8183fcbf74fd3b0579b856577177d9cb307d42" );
            pt_len = unhexify( src_str, "2b9179d21cb884581b0e4f462455167f1f7899717245d4aed3d8db5983daccccebfc2130a20c284563bea5997cc0438c83d8fa7bb9e3588efed285a0fcc31456dc9a3122b97bb22f7edc36973475925828c323565e417ec95190db63b21881016b5332f2e400bb4724c86a8ee0247149370ee5412f743dc6bf7ca5bcc31afa0f" );
            iv_len = unhexify( iv_str, "f2b0564705430bc672964b049115e122" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "3fa342a76cb5d501e6a6fade14aab54a76620e4ea2287147d4ca2b9d62d2a643591e5df570ef474ee88ad22401c1059e3130a904e9bf359c4a6151ff2f3e4f78ef27a67d527da8e448b0ef5cdcfec85f3525e35f8d024540387e4cdcb1018c281a1af7d4a3688a0fec4d9f473c816f7d4c4c369f70d7dfe8f1b7fa4f581098a1" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "18f186ed1ee1f4f8b29db495587d0ab0" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810240128_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "0dfa834e98b6c51ee925dd9edc9be72c209ddcd9099ded57b533f2236895a229" );
            pt_len = unhexify( src_str, "7f4e4f11091bf51976c0fc71ecbcd0985cdad2135549c818c09567801d8a9a42c719aab7dc2cb58a10b5067d14c52cabe6bb9b939e7b9cd395eaf10ba6a53fd2e6446e1e501440134e04e662ef7ebb1c9c78bbd3fd7cb9de8b985418be1b43ebb5d7902ccb4c299c325c8a7cc1de9174f544bc60828c1eebad49287caa4108a0" );
            iv_len = unhexify( iv_str, "a101b13b238cfac6964fd6a43daea5a7" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "bc60d2047fd8712144e95cb8de1ffd9f13de7fda995f845b1a4246a4403f61ca896bd635a1570d2eb5b8740d365225c3310bf8cea3f5597826c65876b0cbcfa0e2181575be8e4dd222d236d8a8064a10a56262056906c1ac3c4e7100a92f3f00dab5a9ba139c72519b136d387da71fefe2564d9f1aa85b206a205267b4cfa538" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "c4cc1dbd1b7ff2e36f9f9f64e2385b9e" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810240128_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "ce59144b114ac5587a7a8079dc0e26f1b203338bb3e4b1d1d987bddc24150a82" );
            pt_len = unhexify( src_str, "bc7aa1b735a5f465cffeccd8dd4b0a33a571e9f006dc63b2a6f4df272a673bb2cc00e603248ab6be5627eebc10934fe4d1dc5cd120a475936eefa2c7bddea9f36c6c794d2c6bd2594094e56cac12d8f03e38f222a7ee4fc6c2adffe71c9c13003e301c31ff3a0405dde89bb213044d41782c4bb4eb3c262595d1c0e00522047c" );
            iv_len = unhexify( iv_str, "fdc5a40677110737febae4465b1a76cc" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "084c31c8aef8c089867f6e0ce6e0aadafa3016c33c00ca520f28d45aac8f4d02a519b8ebafd13b9606ab9db4f2572f396091bc5a1d9910119ca662d476c2d875a4ab62d31ff5f875678f25a4775fa7fc85b1a3d442fb2c5047a3d349d56d85f85f172965e6477439045849a0b58014d9d442e2cae74709ed8594f0ec119d1d39" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "4c39e0d17030a5f06ecd5f4c26e79b31" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810240120_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "e7a6b459a5370ceec4d429bba9472a49db07697dc66dbc2f294d3e62ffc8aac1" );
            pt_len = unhexify( src_str, "cb959e5611a636317feb5265d33b315c2f5af64159029f0032e338babbdb0a525ba6b92cb3be7db9f0077561e6cffe1247bad32dea8918f562dc3cd83225cdbcaed652b87c62fea8eff153638a3a14ef9f9a88bcc8c9a6b65fa9dcc53f63d1b14fb9bb0baf17e7bfb95690c25cca2c3097497e41f7e2299a8518d5d1c5f6264e" );
            iv_len = unhexify( iv_str, "92468d42ad377affa7e808d95d8c673a" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "599dbc47e2f2e3b06b641c510b238417b01869f0e7d08619752f6d9f4b08585731deaeb439ff26e02d7e51b45ca5e3d4a779fe4cfc9572d1d6407f98de69a8fca60bf01d1a769130bb38a67933a2be3aa3ea1470d8f32a34dc863dc800feb7ef71588edd9489bd59a23685ff5358f9b562fc0bbad9e11db7a6fedbd79225539d" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "e853262ed43e4d40fea6f3835d4381" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810240120_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "9818904a99e3d80c95dc71a16483ade1b9b8e7df638ce6a4c1d709a24416cbe9" );
            pt_len = unhexify( src_str, "2c073cdc11a8d58fb55e1dadbbc0372dde86c387fa99c9249bd04cb2f2d239de01bec8c8771a9fb33664ee06ea81c37a824525664054173b63a2894d8d7ffc60b9e93052802478a189be5835d979a28ce7025b219add0622f97c9bcf3ecf629b56408ed002a141061320400409345e94a7a7e3906611305f96f2abc9d62cc435" );
            iv_len = unhexify( iv_str, "96a301ab6bc0309be9735bd21cc9e10d" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "4876e449b0cac09a37bb7e4b8da238f4c699af9714ec4fcf21a07c5aee8783311a13149d837a949c594a472dda01e8b6c064755b6328e3ef8d6063f8d8f19cfda3147b563b0f5fb8556ace49cb0f872822a63b06f261b6970f7c18be19372a852beadf02288c0b4079587c0f8eab1858eeec11c6ba8d64448282068fddd8a63d" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "e1e8b62ce427e5192348b1f09183c9" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810240120_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "9b34f137e3f37addad8a6573b8b6dac9a29e97db53c0a7610f37c72a0efaebfa" );
            pt_len = unhexify( src_str, "c1e09c432c68a2c119aeb3b19c21180e3c8e428e12033f416a92862036f5e8a39a8893b10fe5476e388d079143ee0b79b183a3400db779cfbf1467d69887306b124a8578c173cd5308d4448eefcf1d57f117eb12bc28bd1d0ff5c3702139655197d7305bda70181c85376e1a90fb2c5b036d9ea5d318d3219132ea6c5edf7b7d" );
            iv_len = unhexify( iv_str, "50dddb2ebe4f8763509a63d07322277e" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "793e1b06e1593b8c0ba13a38ff23afaa6007482262bc2d0de9fb910f349eff88d3dd05d56eb9a089eed801eae851676b7a401991b72bf45ac005c89e906a37ed7231df4aeeeb1fcf206ca1311117e7e7348faf1d58acc69c5702f802287083d3ed9e16cf87adcdfa1bb0c21c40c2102fd0def91985f92285e6ea1cdd550e7f50" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "b3c6ae17274faaca657dcb172dc1fb" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810240112_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "66b40e2e671bdf244b45644d1a5adc63011b32156ba9f5e03dffacc1a9165061" );
            pt_len = unhexify( src_str, "985546ee12ba89d95988ad8a4153c4f9d3c91c0e3633a95b4f9b588bba0032006c93210514357c91d574b436da13dc9f68194a981e7b65eb79e56be9cf1dabfdf531407727c034a3c7743bb22aa02b26f159c2eff3c7ed52027de2e8b8b2fefb72c04fbf20a1ffe10d6dda790a9812cdbe9f2ed6706d7a2639e851a42870efb8" );
            iv_len = unhexify( iv_str, "4e090871e889b4be36db5e1df1ea283d" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "f93eebffeddfd16b4618b893d57b459b704b894b38a5eaf6cce54026c80090be8328e12261e1b10e81c73ac8261c2982bb25603c12f5ffff5c70b2199515c17200db2d950a3f2064d7b362607adbf3686f27420ec15e18467e86faa1efa946a73c8888b8fdc825742b8fbec6e48cdabbb45f3cd2b6b6e536b6fbf3429aebe934" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "ed88c856c41cac49f4767909ac79" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810240112_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "18c5105a9651144ce965b4270398b982120b885850114571ef8e2cbc5d2f5e04" );
            pt_len = unhexify( src_str, "00c5ea3d91248bfe30c5a6d26dbdf0609f977afcfa842b603c1061b2a473c9a79b421b2509550309e4be9c5015c51c6def9ee68c242f6e206b3027ce8e58b7ab96aaa50ced1d78c2dfcbc2589575bec2ce3b6a5066276fe7dca4f1118808d1e5cac062667053c15350289da03cd073377c2d66c01e3098ed01b75788c7e1f9e7" );
            iv_len = unhexify( iv_str, "a3a5f82748acc887e33328fd7f4ce1fd" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "d91ed6886a269dc1eb0745dc4b97fc54cbea5e6857d10a303a3caf828b4e0e20bb742bca17021b7852d09a6d7d3a56ad82298c15a2082fed0e0e326bb16dd677ee262ead93a24147de3c07eb8a95b108abf17357155f1de79171689407b6545c9fdf8ab4486576490430c0e043e21e7c40ce88e752cb006cb3c59479a7e56cf7" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "add4e086d612a119c6aae46ba9e5" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810240112_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "4667cabeb3a644e371cbbe9195413daab025cc6efc12298bfaea0dd9bc028f9f" );
            pt_len = unhexify( src_str, "9772ec47f3cd26f091bf117e085f2394db258c2c460dc3b1402edcb60a8f70517f82aa669607b78c2ad79c662c3b376cee1b9f34c4ec5d15319c33de78a440e7f2a4108c3c9da51604adde2025ff1dc336c49279c13a7153931df675df0e78f17a4d72973311af74fe755c85c7869baf3896bb738925942dc67f1b6e690c9d48" );
            iv_len = unhexify( iv_str, "7e8927c69951d901494539ab95ac5906" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "5d62fa69cfbfdec30193408dad15cf983ad707ee921068b817676eca9f70f9ca4623a8c113df5fba86131415f4ec546c7f1a94ff9d02cb8ddcf421c7cc85ed87ce712fcd8d5f45460749ced0d900fe0368c59b1c082bd5811c1a648a51768d5e4bfbc23cada3791f289d8b61fd494398be1ad9ee9ff471abb547000ac2c1a5d1" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "0ae6bd5e8c25d1585e4d4c266048" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810240104_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "3d58cd514de36ca7848aad1bf4d314b3b3415cae1ce9a169021ae84a67d4ab69" );
            pt_len = unhexify( src_str, "e1c2e79e3f64c5c64f853ac9ba1a853fbf1bfd3001d48f7e73e0e97aa1b8ed1f1a7066178e75df688c5edb1c42e270ea38ab0e246c6a47fde4c3141436fe4b34beb9033ba7eebfc53cf1f6c8ae1794e9bb536152d196e1b96803316a05f1dcb9016c8b35bf4da06cd18da6243acc3a3dc641d3a1332b1915932ca89937cb0327" );
            iv_len = unhexify( iv_str, "4a1c2e7a3f9788c3c2fdd0dcc0cfe84b" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "50d63c660a2b4f8e87276c5f58556cdf15d0fbb2c8ea5e3266d28c515643109aa7fc950d6d48f504dad52457e16576b581d37574574cd8b7ac12b7d59b819992c941a27e23ef9f257ed0c4ea4eda6c1f3b28b44decb63a92fae84c3556dcb9d6458e729dad6a7db9f7411690fce971b3b240f8f9979ed992f87d76e227fd7384" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "ac842579bdd1ac77c84dffac2d" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810240104_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "b7e4cd80f03a7ed092c776b243dfad7776d9caf3e679939038e33ac94d8931de" );
            pt_len = unhexify( src_str, "102e2d2c0d01dbc69733d2451d1ac1817d60418685d4ae8aa44e1ede1c1e08d2f71f0aef41a72bd9f052ea4a9a057330c95d964f8c3679b80fc9c0952b46f38e2ef055cb33703d686757400210fa5a39bc7e3bb9b8b9cc20c95d5607e2f10bb5501507680ef3aaad96553333b1d27bf2f7ac102c983eede2262a5c6237c1d754" );
            iv_len = unhexify( iv_str, "af160a983d674b7d19294f89c3c9307d" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "6bdfae299d796ef36850327b091ba7bb02e29b643ca4c8bc199eb91ecbaf88426412cfd5570e0042cab735cc46ec648b0877955b3f9a5707d56c478aa77ae5510749beb1e44dbbb37791f18477123436a985e5e9f79fda0a057504847e4ecae841f24e1b53076d3efc6bdea2ebb336ee0e4b5e6ea973e3e50a27b5c2e6fee3e2" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "fdf21e2ac356e507745a07fc96" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810240104_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "3a0c46eacfe85cbc0c5f527b87cd075bdeb386d0ca6de816a87cfddcb8a87ae8" );
            pt_len = unhexify( src_str, "6d1203dc8395e35a35e234203625ea9d37d1c009db2ac8b1d5b29021997b5421f1d172f4c9a7eb7dbb67f0002720fc412f5b1550c739a2d7ba4387a1f978bd548fe6169d9473893782b10fab99198cb8b4553dfe27583c017136fd8c95070d8d7f9a602d15248d38d728157a0b26404e662f9a5554d3e1582bc0e12f0054792f" );
            iv_len = unhexify( iv_str, "b1cde63ad2ad4b8a7bfb36ab78385c3d" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "9de3a45c976d32ed2af5074ef13b1f86f35b1689b1c698b2e427d5dd62556eb14439f77cd8fcbe686a9a08a922e3f54a78e86fd284de493a740586360b63da09bc1d001777582969c679db54a0ddb8d7dfdb46750edc882804a1c00e417912b72b4cad54dffa1897eba6188b3e61ebf0c3dfab292c2686dcb9db3012e0788c7f" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "641896daab917ea3c82524c194" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_2561281024096_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "4d540e0ba27103667eb4511ce9d243592bccb8515ab59896c9922cb5f1b47a02" );
            pt_len = unhexify( src_str, "d79f9b1c74e3141f188704c8d5bdaaf6083642be50d00f20c97b56646863895250d131e00db0ecf4f035d42f08cfe20f401c2d3062a38daa0b9e7c19fa7c5d344680aff48d506daa181451f6b34ed9099b9a5b39c0166e93ac4463c9ad51f48e3063b1c16793615336f55d516d079f6c510c2891b97aaa95e5f621e3b5202620" );
            iv_len = unhexify( iv_str, "a2ed37daa797522a39b01dd206d06514" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "6a891bd289ec05990424a2775287f4725aecefe1ab21fa0ca643f37829cae9fcbbf805b883f807102ff12f1a85964df818057daedd41c7349ef32b24642186c45d2858c3260d5b90594969e26b691963ac7fbd2eb4eef466ae690ca274d9194dfc4df1c3baec02abc38fbfc0e2c7c4fcafed227d4f6607329f57ee439435c714" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "9074ecf66bbd582318495158" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_2561281024096_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "151d7e4db9e21c87bef65c2ac6aab5b6b045b7dadaf6424644a91e04ba810585" );
            pt_len = unhexify( src_str, "0984c5d3f68beba1db4e6ade429cb8954cccaba9fcf4d852897ef69f8483428932c8f18a891f54b68f7d49a03c57f7144d802eb996d233cec930d5eb19f43d0faf9c94a2d7aaca40c8066a2882481f521bb5f6ba15b213810da373817eab3d52b5dd143a1521239482fbf4a07fe68c3d35c90c6ce27b55e40abcf432a261dc58" );
            iv_len = unhexify( iv_str, "49e0e0d089e3574fa5a33c963b403ccd" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "6938d8a7625d1291f249ef1e086bb030ccdc844a9271fee16db60e7acfe4aedd720de76345109d5e6849fd1576c0fe0c34e73dca4011f8565cffccef427198c927f19f63b821f43844d008ceee0566f0d8062d7860e92ebdf21dcde80039a04504cd8ee94874b2eeb038962a74ac9902d9d7ce09afdac7aa706bf3892de19531" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "48d3a8116213f92bfbe86bfe" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_2561281024096_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "3e9615515ca45109316cc02bbf3a23406eeeab2092dc6614db76e4e047a3b023" );
            pt_len = unhexify( src_str, "46c4c6bad0f21172094ae07a47fd76477b69ca75cc08970e8dbf7b8644d4bcdce96f9d15dd3fba5fba3f851af145652ad004ee525d180d2f3e03bc0ec1c0e8ffebc1474c342732b7247f657ba87ffcef9333857123f29c4976b048c89c24107529dc5dd69004fd176eb0ca6ddae1df7be7d28b3b9da976413588f20c1fff488a" );
            iv_len = unhexify( iv_str, "c1facf73da64e16e4acee3fdc3cc6b10" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "4415dc96d3daf703d392ba1318254143a58870e691570ca6b1be6074dd9c1feae12c72f9314fc3d19b6affb59b642ade6c4e64b7c99f850bff781de193cc0a321a29356addcb0918a282e53801541b5b01383fa7624c36d1f67423f02d2b54f58deca582b7031d192a4d32bc154ae1149cb3c5b48538c803a8d01fa7cfc1683f" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "322d8d1b475a7fd3d0c45609" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_2561281024064_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "52c1a14b4ed57cbfa317fe0db87528f4c5551deb9ffc88932589e3255b1d3477" );
            pt_len = unhexify( src_str, "eb9081e19b63c94b5f3a696c5fc2c0b7f434e1574394d0b41dd67dfac28a73d4ba26c86b3728b2802fb9d0930c89586b09602900d33eddc5a00a4e98881b5acd5597aae9b80b1569ede74042948f2cd66c3eeae227ae10241df001c85dfe8a5fda0aa21142ecade76290dfdd4a27b6ff3a932dacc0b5f461501239ae8d6d5f41" );
            iv_len = unhexify( iv_str, "36d02604b5b24f49b08bb01053a23425" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "12fbea9e2830ba28551b681c3c0b04ac242dbbde318f79e1cb52dba6bdde58f28f75f2fb378b89f53cef2534a72870a1f526b41619c4b9f811333e8ee639be1250a5c7e47ecbee215b6927ecffaf7d714327b2c4e8b362b1a4f018ff96f67557ca25799adfac04dd980e8e33f993051f975f14e05be8b7342578d0c9d45b237a" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "01e6af272386cf1a" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_2561281024064_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "4d08a07b3e94025523a4a6415029c8f9e11fbbfd72564964c53b8f56f865af0d" );
            pt_len = unhexify( src_str, "4ac7c27b07a4aebe5caf1de0538d13a56e8c11bc73713bf78c7abbad3b9f6d690e00487267da108e2f2ae67c24b4657e77bb83e2d5e4b244cf34e924cf7bdb443f87ac8cdb374147449f8d06eb517a25dc86f03a389f34190aed5a7faace03ebf646fec2b173b2c15fd5cbe7c5affb6c3ee6d1cace8b00dd8f668a2336da5bfc" );
            iv_len = unhexify( iv_str, "98b745c7f231ba3515eddf68f7dc80f4" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "337693c5c746d8fcdf7cd44d8f76a4db899402b891176e85b4c549c366ad709322874e986d6b939a350d2a0e3b77924d6d15454d882d1d3c94469d749a20d8f0116504cb31888a1e81d3abf25dbb7a7f9e7def26b9151ee649c059da1955f1716423c734dcd26a548844abb6b64c44383ec698e59361b6582c6883b77c338342" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "7a9266c4e5ae48f1" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_2561281024064_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "b9d9fc42b58deafe9bc9734f4129dcad34a2e55ee5ad8abcc3f7bc42dd2c0e05" );
            pt_len = unhexify( src_str, "11dbcd6cd53d2af766a1b6e4af2bc8bac2811ef818da2d1f81c140ab6e0298e958fef033736bc6e0dccd660b9a3e4222bdf3f89a95b206785d22852201e6dd00b44232ef3c03393893813dccf1960410b50cf50602ead8bd246fad88e66c88b50821578004779b6c45c13d8211df1cfc0fb2d7a342f58e4f2f3623fd31b12c30" );
            iv_len = unhexify( iv_str, "67931493096f4550633c322622bc1376" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "66ab6e7a547705d8ae8ac3cb9bc5fbbc18cd220f89aec7dfbf4f72e7bc59b483c50c9471523c3772efc5deee3a9c34c96b098842cc42f9b7d7c0d2530f45900eeb9502e4dd15363b0543c91765121fd82fcc9db88fe6a531b718c1fe94b96a27856d07707fced3021cca9cf4740833d47091797cc87f57f5388b48e2296ff352" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "0de60d4126733404" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_2561281024032_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "97e736a63870546ec9c2325a8e367c8ea17a7ffa71f6cadd6909a5bb9eb12814" );
            pt_len = unhexify( src_str, "608280a9dcbd6dd66100a9fdd00e6dac2183e32c945b2b4d255c048243bfea15aad1a10ff3eec0ba79c531239b489a5dc155dc2775519f8d3d2ed82fa7ac653fb7c77e0dfad1c175b6c69963f5c12ff9840f18e0202502e9d1e3b170965cd86ae411af20e6d69a608c99ca8dae3cb3bcce666841132a99429bcde490d9f0b6b5" );
            iv_len = unhexify( iv_str, "d35192b4d233507b70c6d32f8e224577" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "568a0d584fc66c876b7beb9ef8709954a2c426fb8c1936b9024181ca2cd3a7684c412715c11eab80a181be0238e32a2b689e9db36a2ac87db651058080531e7b1110938dcb09615e385d7b224b11222469145f6fb5f4c0e87b08bb3006bc5b6d2ce0a15be7fc29b27c10c645afd9d8253c094fc0f775086bdf2adac265b474d7" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "af18c065" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_2561281024032_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "6d05193cc0885f7b74057ead3a0738b74eb3118b1a7e74c5c941ce0011197122" );
            pt_len = unhexify( src_str, "c58f51bad815a43a5705c311de4a846ea2a70cbdd2c30d709a2ae0ddf82b7c889dc599fb6e0328fad21555a99530be6deeeb5b1beb333322c2b747288e52fad008513f8040a4735cab3c8cf32c4e18bd57339c85cf5dd71e382067bee7e9ccaf68e767d77fb005a3b73a51acf942fc3b2c5c9eec6189d01a26c6ffb070165874" );
            iv_len = unhexify( iv_str, "5160b65bf7a2ccf77fa2e3e0b3866f26" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "64dc5834a63be414c3714f1b34feddbacd568c6466cbd06f665aa269187a160db79306a53b629fedc1247bd892998fe3208b3105f6273676bbdbff6e254de332d02bc8842ef98d6b79994792eeb5be3a807452b14ae5b5027db81421cc22936ccaa7ae1b77a145462634e424ccf2dfaf001ed4477b804e204120a1416b449b8c" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "364ef0b5" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_2561281024032_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "6e8006983712ddfedfebf95e6cc3b0aadc23077055e500ae49fae7705787f2e3" );
            pt_len = unhexify( src_str, "e3ba14c4e39ebad925997649872b8331f1700c8f98f80e58d92c85a84f2a427094d9d771b276a0d35b17c0c030734399070a57345d4dcf082b96c7eb580618f7af8bdf036296e20379e74e29f905b52a0c46fe7d46201a075e7de7e1a523a0492c1f228102fdb89f019bcd4571e041c5d37159dc487ec139fa37d33142fc8082" );
            iv_len = unhexify( iv_str, "e36e39d787394f1401fc4b173e247db0" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "4d5db4b65a1ca31f3d980cc30037b5d79d28280a31cc5d0274be77dad70dcd37f652f2ca999c9aecf08fd2a02d382457a277002a1a286ab66f9e437adee00c3bab04f831dd52147005a989606171b6017d28970c8986899fb58900e23d1bc6a9ac0bd4d8b5d6e3fcaebc9903923e68adae7d61cf929388e0e357c7223523d1ff" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "d21637c0" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810241024128_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "cd8ec237009eab590dbd9b31e76513dfa3501701b1a706982944441d996e1839" );
            pt_len = unhexify( src_str, "9eef7c9a0fa3e9a7fcc4b2f9d210a97d6653ded7913f2fb2de825a0dfd78ae1cca68c040f2328009fffe62937d630ee9d6e0e67bc12c38c0b3d035697d4c2311371aacf41cce0d523016ee436a47d93af0df77011131856d072c718c310f0995b71530d70a3da881481f46f21dda62e3e4c898bb9f819b22f816b7c4e2fb6729" );
            iv_len = unhexify( iv_str, "a3cae7aa59edb5f91ee21231002db8e2" );
            add_len = unhexify( add_str, "45fa52a0e8321d82caea95bd9506f7331923e2aa95e9238908f3ff30e17a96389dfea75e225e34e1605354eaaf999a950f469c6e2e8722da5ad9daded6722baca00e5d1b8e63266ad1b42cae161b9c089f4ffdfbbaa2f1fb0245d1a4c306d46e215e8c6c6ae37652a8f6016f92adb7695d40bde8c202ab9c2d70a96220b4b01b" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "833d58f0bbd735c6164ecaa295e95ad1143c564d24817d5f6dded5d2d9b2bed2dc05da4a8a16e20fdf90f839370832f9ddc94e4e564db3ae647068537669b168cc418ea7d0e55b2bb8fd861f9f893a3fdba6aace498bc6afe400fea6b2a8c58924c71ce5db98cfce835161a5cf6187870aa32f522d406c52f91c30543ea6aa16" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "c1df4ee60b10f79173032e9baaf04d3f" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810241024128_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "5f0b24f054f7455f5821fdc6e9ca728d680e8004fe59b131bb9c7cddb0effa51" );
            pt_len = unhexify( src_str, "d406138587fbcb498e8ec37f0f3d7f6b2faa02e6880424e74cdba67ae3468b6823d37fd917a7fede6b34a2f0fc47c520e4088766ba82a989f0d8051a3a80cc8b1e3e1e2b1c6620b90e99b27e65951aeb3936263fc2f76c1c8effa742f53987f8a38c731a411fa53b9f6c81340e0d7ce395c4190b364d9188dc5923f3126546c3" );
            iv_len = unhexify( iv_str, "f52f7a2051047f45ec6183b7c66e8b98" );
            add_len = unhexify( add_str, "756cf485b6a8e672d90d930a653c69fdbf260d3ea18cd3d0c02175d3966a88b70ab8235d998b745a0eb6a5c92899f41e8c0b7aa4ec132c8cbb1bac97a45766a03923c9b93c2a055abd0127a83f81e6df603a375ca8cc1a2ee0a8b7fd226226b0b19bd2e81f73c34dfafa4fcea08dd93dd4ab7e4b437408af91bff566068a5f34" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "e58a03f664003d0ef5bdb28931afd16e7747cff62dcc85bf4eed6e573ea973cf615e4ebee40f35d44e18e391b391e98dca5669a5b0abbfa67834836b122d1909b53acd50e053d5ca836894414bb865b1fb811d8af68b88b4a302fdedf27fdd27456e9aaf34a8d53c9c8587e75843e09776392dbb0501ef41359c01e8980e5221" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "258492b9f549d1b90555eafbe5292806" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810241024128_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "6f50efb3946f6a6dfe63f12780f764bb6ebcf2127d3804610e11f0bd9b68ce0f" );
            pt_len = unhexify( src_str, "bfc89d5049a5b4015c9eb64fdaf9fe9f4be7229e67c713a7b368f0550b3a5e12ba3a4399c64f60b7157e1b289b154a494deadecff0d0686ab44fae2a34ae4cb120a7f00268ab551f41c16a05f8999157be1103464127a8a9bccf736c32db045124178c90472e664d8e67a2ade0efe9a3b048c453d2fb5292dd8d29e62d52c5b5" );
            iv_len = unhexify( iv_str, "63c1192ab7fc75c17e7812fd960f296e" );
            add_len = unhexify( add_str, "335cc5c8fb5920b09e0263133eb481fd97f8d9f29db8689fb63034bc40959a176ccdca6725e1f94f822e4d871138fc39776fbe062f07bf80e5c8891c2e1007efeb77c158ced8d6c002b04442ed35c40a2187a59c02339c05762942208e3be964736a431017f472dfd5fdaf8fb8c645cdb684f9632057b9eb755253b4b75e3688" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "ca974942ae0f4955ca0736218e4e356145c1ef42135b1142b55ccb3fc5caeec630eb50e69b5a6f97c11d4b604189b27496623bb0365ae69f4150e201e72bad8e7b883185588d0a31c44273bae87194b1610114a83ec47ba68a02e29891de43204977fcd0d551778335fc77fcfdf3fd63e9e5e0c02930a0321ffb093c521cd0ed" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "2f11a01cb0ef8dcefad9233bec44d6f0" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810241024120_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "ec566324ad9d4cd015821e2cd4ed4d3d507bdb3c65bd50acc85f690ef06740fa" );
            pt_len = unhexify( src_str, "348d35768d7192415cbb92c5625f10edd79f24c56d4b821aaf80d7dc83e901ede6be94d1efe11a3acd16ac00aea8d0d4875c47522332fed11cdf0816b26978de431c89d2fe6d122b2d4980f1d53a97edc15e490a44e73cba9394ca4bbb871675c729c39de80d6678c71b1bd220e4647bfd20a7ddbefe2b7eec7276b87c92ba77" );
            iv_len = unhexify( iv_str, "95c8a544c4b94e9fbfd76e66f40bb975" );
            add_len = unhexify( add_str, "fa6f38f8e562a54bb2281dc9a7cbe0b981292fb00dc0053185550a300661852179d0f2beb4e7759b81316fbfead5c858e6fce73f3cd2c2462925dbb199a4e6c121d051b1b5ebf60e16d1e30f6973b19cf31830da30588fdfff6115a4a1f6d977a72583379a56055724581be5232b0d1b0ae88bab5d4a031b058bc8d03078dcd5" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "8b4da79f3ae1ea35a80af2f52fc640055e6a3b92617ddfa79fe5d8a49f28ddf36a82a17ca0b3cdf1726700f7ffc09ae5b412d064fd52a90a76bacc74a0b89e38dc474e880a2b768ffa91fef34c47759a7b8fd7faa32a4fcb258349495e4438c7b2055a8f462729fa4e7223aa9b47087695e3aabf43afb32e272d536b257b748a" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "b1faec277697add8f756391dd9c7f4" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810241024120_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "dd6aa4ff63efad53772e07e0fa7d6eda5e73be167620fd7c9f3997cf46cd25a9" );
            pt_len = unhexify( src_str, "592b3a6f09841483770b767bed73498c286896d2ad3d8bd91f83f92f489b1e83b0456a54e067a79e1bf59eefc1d3bd35cecfba940811d06a06e9b8f774bfeff557bd7e3f0864cb6bd3f867efbe3f040d2384ae8e1a0e20ed38caa668159d3e33c4669478d00963a1152305aa2037a5e06cac52d84021234a7f5d46ab060bd03a" );
            iv_len = unhexify( iv_str, "6386e03bcb6ac98140ee0706b54c8492" );
            add_len = unhexify( add_str, "0ccdaa4f54cfea1026a4d26338b1e6d50a70b00c46147fe906c95f0a2fb5d92456ca3aa28a257c079eceb852b819e46646997df87b873bc567f69a2fae471df03b0e5b94511189eaeedd238a991b326963c46d53080f420ec9fd1a74145a0b155cbcc0b5e47fa69450c7eb447080e34868d640f923923b91a9e13a05c73550ca" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "c1be540448f1e3f432a10b3cc1a913cc4046595f5a57bf57c9d856cdf381832e914088d3388199018ff26327e3001678ab363da9457ba2084f5aa81320f1a0343491e0b44424018765861c5db917ce14e91a77f7e805d7a97a17a288ee66567c5c01ee61dc46a9aa8b281438ed377b792e9539e311676f81c567339cf92b8e1e" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "ce7e361713630ecaff81866c20fce6" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810241024120_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "ad3990cd57ce4e95342cdca4f07d7e35d575eb19f224a7c821b1f5a8c54d4bc3" );
            pt_len = unhexify( src_str, "732809c29b5eeda974039b122b875aec2823e082ef637294658cc54f9bca88eb7eea87a366234f89919975d0e7dd2f8ea83198d5a6e349149a016a4b177ba43df2f3ca28e27b8566591d225ac25dfd9ea431cf1fb3ea530d65dac93aad47764a6aef8ec6903b6d145ea9a2663034d2a320690b92afd8032084b754be97604382" );
            iv_len = unhexify( iv_str, "fd4ed75d861da2cc14fd1054976c8566" );
            add_len = unhexify( add_str, "ab44689839fdf47e887b70fc1b0422dbbe5c1b50f4e704f9a435967ba8b70cf1e144a025d37292f628f9f7dd9d05557b65340090503201e8cf2cea2d6a73ea4850bd0931b90fd4a4306ba84b8aec99fed47ca1b16daee6c95c97e4ba0dd1fb130cd13f5ef77c5af96f61fa05305a3aca3775e927f72f08fc34bc994e69abaad8" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "f48721b08101b35cde1c4ce08a8ba0049185b9dd48b66ab9971fd67dee24f89b456e9ca19ac8a9b5b3b088cbd53898a8c2ac1129752fb7fc55a0c3e2e7266ff40f7a9d63ebc4ab65f47422fc17cbe07fcfda582fd1b8f50e840ae89837e84add8be17d4cac3d2be26bef4aa8438daec9d2b139e442f99c32f2789378c8029ad9" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "da6da2af0fc14b591a86359b552e20" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810241024112_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "30823396ac90db573b6587676564d09fa680906bd6eaa6b8597e2e7549c9d848" );
            pt_len = unhexify( src_str, "c55be5a0b8559e02de4667ba5656f7e46f5627af13fd34d327f6fbfc4f3a9273036fce2fb21232f8e2ed115b39b0ecb9a119c8fc17070bbe4e34d3544d7117ffda5e1ef05e063b5a8fceb23158d7824d6a1eb4d90a1d0360c6bd78fb24fdd4cfa35924beb4e090891d06f53fc52cdcaa6b8bba6772d549eb95b64ebf3756ae45" );
            iv_len = unhexify( iv_str, "496ac734afadcd54f1a4372ceb5645fc" );
            add_len = unhexify( add_str, "2d582131f7071e80cde1b11106b7d79bb208743de759d40b897efdab018f4eff1f91d2fe67e27af25a13f201bbe4446f20ac6b942ff7b32cf10ad1cea36945b67ac08b114fc616175a87437ee05f3a8b6566e9edfbc1beec0ed8696b5d5c41a25ac43bf3ce2920dd262233ab3405d46f523894dcbfb6c90b6e911ceb93bb7fa6" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "c9da3df66111dcbabf731c6891eb698ac3283780f526e81383e201244efe4eca7a1c84a3bfa9ba5616afb15c1f1af0f3af2e071df6c1d34a343c3e3440f1a3e1b6620243d9e7d9a4dbda5981c3e876fd07f392d44bf3e0a4edbd884462ec2f71d36bde4a1b5792629da09a1fb01bfdbd532fbac71887a05a7077fc119a4638d4" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "cec973a27c42e31b779a6a91aa34" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810241024112_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "815f2b2f0b1621aa198eef2761380f10ac9872a5adbdf6286bdf3386e56aae4e" );
            pt_len = unhexify( src_str, "d16930c570414bb620e0eaa2e9b5d96e4424127e16461aaa5885c616a02ae974fb2890e73bade9ffa5066eb88a46ac7fcf258d55733d315951b1b71c5e3c13d78d60344ce921966297a0f6361cfeab03b346a7fa4f83a7a0eaf37576fa33a496102446f9f31b06ed91b51672c879cb18d4e38fa86e156d5b1dbff27925922470" );
            iv_len = unhexify( iv_str, "0843984bbaa565ca24f148e57a7d9c57" );
            add_len = unhexify( add_str, "1514b99c0ad3493c36fe1216d1a887a69ea0340101aebb03f60d7ed26893119e81e8b8c3f0bb4af5e10a3bf4edcf257473be9dcebb44a9d912f04d97a556ecf020c0bed7ccef2bfd5580f1fc74b706fea45f8c63d8de6f8deccc47a02dc86d3f0624e52f6f1dcd09de8000f2d98a4cc0896da6a564b92263673adf390ed909fa" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "7506175acd64224b39f890e498ee5013bb46fc571dc2b125ed5891b8ce8bcf42342f015fd2df5f4b9cc220aab52386bf2247d4163951e86467633f96c28bdda166d778855a7f60465dd2983232c9e53d5f89432407807b0402a10f155f80055c339451a106ac54438ae4a945e60d5320eab0adad9a1e66d59b9d3cc53887811d" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "28d9d780052b36dbe80a25d41d5b" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810241024112_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "d1325ecedb8fc0fe449de558fbc11ddebef660e47aabb84edfe69837a6a9066c" );
            pt_len = unhexify( src_str, "f9a4f7029feae5cf5bdb8385d6ad7d7da6a243c5026818e5a794c6cffb8dad3227964501c5a049b5a94a7ea2e24434e086800094118444c5a971bbe575324fb6b51c5939f81e78bb11d85d324742b462ce8d13584b3882617d0c94776f328a554f9d532b6515ade9fbbd2de1c12ab53671b7f7edaa7e20223f4c371c1f229568" );
            iv_len = unhexify( iv_str, "8aff702c40a8c974cf24bf3c645169a5" );
            add_len = unhexify( add_str, "9ec2e851dee3834d4843aafa740f3aac4cfb1e4d3a7e3e77349113f5200768c3e9dc37481d6292ebeebd2372db02ef8ac7180830c7187995c815d1d1520c3e2f8cf2a94993b18c828b53485073c8a845066772615b26d7a3d7d3e7d81ad1725797153f7ba5e313bdec582c5482adf76b31c871cd42a313018f40d7e23f1a7f33" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "3a93663aab93c6cd236cba4db2c03942d9ebc669633936370c2834357e76f6555c34d40dfaab1e78a105da9092acdba8be89e2dbf72e89518d55e09eb2fa1ea7da505484ad4531dba3eb853d1ae1a477355ea9448067b0adbc782d64ec342c7cb781d9dd8dc2b14dc1c9ab5542b679782b8bb9b45ff6a4e36c513df169c8eddc" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "7e682b0ddbe6c55091838616c352" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810241024104_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "4b92242268e598ddcf3a5a0de26d74356693c4dbca354e44be401f3d6804ea1e" );
            pt_len = unhexify( src_str, "72dc75bc4c8f5bbbd9c639fbdb34afbb84706404c9e67eaee1959aa4b51eac0db4f975cb3ed8d8ca27f72f61c8562ec953a7b8745826121a7016e60e877dcdb046f236af3826c1ddf5b929c5bd9a92b0d5c23cf8983bf2459ced6595882b3dd0cd25da7eba981bba122623dae22dbdce05cf4e5d82d2cc54eb4f68e9e8eff02b" );
            iv_len = unhexify( iv_str, "3c292bbcc16c94b0a263f4d22f328915" );
            add_len = unhexify( add_str, "167dfab08aac8350574693b31210138f6b99cfb61ba7ade2e2abffe2255837a913c9afe332e8fc4b2463310df46492e7d982dcb70fdda2a8b03911e6be9a5c5621d0ae8ecd1cb390910b6702aad33394c25d1160b86687e25bb6cdc4811e3158bb85ba75548329dacc19287d9c004a0473029b77ca290fc47c1f96d9583bcd67" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "c2dd42ab9bf3fda78032f73cbf7d28dd8e32c582a3b7ee79795551f133234d62ea6571a466b8e1af0b3d354b71a6582c9c8013d5f8a2c34eb3e848360adac1d5005cede58eae7784f32a31c40eec5a3f03cc1e7263d8515b36225b3515ebcf8dca2a77172c797d347ed3921ca0bc73e8ae56347134a6a2a06ae084f1ebb7b0fe" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "02fb002d8e4a1d11bb0f0b64d7" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810241024104_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "c5c50059a61692a8f1ffae1c616158c67d276dcd4a029ce197ed48567e5ff889" );
            pt_len = unhexify( src_str, "ab7e13923e66d0f600accd2462af74192c3de6c718a27052ef7c1302239c7fb2413df7c662657ca18228575ed138bc54f31663df548618e98d64402feab529d5bf6a678431c714df1fe24ea80017f455a8312bb5b710df8dd3571970404a806ec493dcb1f3f1ac980663f0b9c9823e0d0304ed90689f70d4a24da7d8504c5b0b" );
            iv_len = unhexify( iv_str, "920d82c6b97a7bea121f64f83b75dc65" );
            add_len = unhexify( add_str, "a9bd57db2bbe83177287e5f614dab977071abfe0b538067f7d0c5acd59bfba95dfb725b8e1af4573ff10ce135148a3bab044552348378d5ff0c4f8be1aef7ed60bb9a374a6c7b8097d7c1804fdf078f212e63e9f11d7404ad0d1a9cb28d5ba199aec3a6c41b9e523b541ad38cea763159836ede6371357ab1aeaedaaf4481c29" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "8f7e87e3ff4f7ccd1cedc1df125199cfb588339119a5ea5f9bdb918f89ca35f9dc16c6465fb25ea250eaaa8e7f00aca2199f92a2c244642bd15cbc9b62caa58115ef01d0b4a9e02527e035744b20892f79b07aa47b6c6db1332f82434764c43124b27148f2f611766781df8e4cc0b5ba99b858c13c233646dcb2b8749a194f08" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "65da88676d2ab3f9c6d590eb80" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810241024104_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "4c7cc3588436ad9e877de72578d30026d32746817ca7a8fb7df9870650aa48d8" );
            pt_len = unhexify( src_str, "00c2845fc495b89f870bce714f8604a7e7a96ede92c4b9bdcf044c9a176f66a28761089c083d5e2d613c746711238477c0efdf475e18af99e88cf76d04d4e40495ea16c462801443cd7f69c5d36ac9f337e828c308f1d1938b1fac732274459827cf9806c1661a247167948a93eb6e998a4cea76bb825baa27e4180e52633bb3" );
            iv_len = unhexify( iv_str, "5e82285a3b332c693e427f9410564489" );
            add_len = unhexify( add_str, "9971b8e234fc3e1e9644545e383eb065e1866e2faa6513278d3972add5ec0e71b1558329fe1ee038a27919e43bfdac8cf08141ab540528f74f9d5bc8c400bb6ee7867e4dbc2aa081d9126ac374dc62b10004d0e233dc93376b93c0da415e7d3e09851f2084a99feeb25939e21893056870cefe7cdfaf49f728a91ea0eef605af" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "ab7bac4ddede796576e1fc265c3c598055827be74dc7ed8ef172d00a648da56727767d68fcbe6c44e7272dc8cb15f03a26dc439178849b0e9ad6c7410dd4cca3f9ef40ec7c280042bbc199155c7341e88d35e5e8d0b42856e618c6c30e43d49506ccc3518585c951a3898409315e8b3b4d0adccdb561ddcf1b9d3b2cf3de9750" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "2474c830c6ebe9c6dcb393a32d" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_2561281024102496_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "9d73aec506e022c0692892f6dbc3b4d41e86b97fb377c1956ee27b9c9ab3b32a" );
            pt_len = unhexify( src_str, "f02bf60f10ed876a803a96e75f3fe17b4e355246135a0cd5497baad2a40a523c27e27bf848f0cb5d0c6428d08bec9590b17fca5e697990d2a6f7d21080ab614f378a07461e7a6207229e0a087e285841ef2f119cac7d8a2d3abbb1e7272a0d7dd493c8c4f797e160c36e086227ceae4923658365b2d3a3fbea11aa2fab3499cb" );
            iv_len = unhexify( iv_str, "bbacc081a6107364dcdac83abceddbfb" );
            add_len = unhexify( add_str, "77e1da090e4d3a892baf1afbc12a56201a4362d8f09cda5e9bdb23411e6908915301d66403acb3524898c1c51d6970a71878accd0048cb6cfbd4bf941c174ee05eca2c4a29f1c24e936d3a63cb6cfa710617af1bbb41d755b2f79e135db914a7dd00c590cf741078eb72c3ab559787213202dcc0a4734bdd612b917e372f0e61" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "d78fa4024b8d073899ac09b8151c29b10a37793b76f04921bdc7dd3d2ef530a831e53cf6a7ddeec0e033ceeabb525bf5ef57bf9b3661ffb57d3bd4024252fa11dd569102c787c2d8489a1ad1290dca2e8edf82fbe6b5f83bcc0e888045b895e20c8556ee80430cc8640fc070491d2bb81a1209428938cd8e7a27e0e858029421" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "2235d00a47d57cfbd383b69d" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_2561281024102496_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "73198dfd92d26283637e451af6e26ff56e3b7d355ed7ab8b2059c1022e0ea904" );
            pt_len = unhexify( src_str, "2471b3c4cc1d6884d333d1c998c7c441808ca884cb88173a225569e1689ef39e266e9ad381926adeafc2daccbdd3c9457ea1bdc3bb05168ef1eead1504d1d44dde34f96e1a7f2a5d3fb33cf5292d52fa9412800419570db0eb24fb74d55de202f5df74073c5a2eb9eb726393996eaeb32072bebb00593de41b97ecbab2554186" );
            iv_len = unhexify( iv_str, "e36403ce1acc63bf50b47387250ef533" );
            add_len = unhexify( add_str, "cad023cfb73d08e5b082c3061f3a6502a1c1d53038cfb19074d0ec26c9b272db93094147ef0ab2bdce440a2b3233bb0429add47601f011df679698264c0f81444aba14576a1a565e5c169f967c7571bfb32a2a4d7fcae897863d78964c5b1a040cc845494c0ad8ff4353317b28ca3798e6252d5015b58e99354ce6dfbe8b7a95" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "32afd6d6fdab2019ce40771b5298aaadf753d1c4cb221f01e4dfc8b1968f898188fa4d448d8364510a7e68c7393168efb4b4ead1db1c254c5cea568a84a997a76dbc925a6c19a9092002629f1d9c52737005232e5c7620b95ed64741598a65a9ec95f2c97b6b78bd85380811c11386074b1e1e63b9a7e99d1cb2807bfaa17f0e" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "e22deb1276a73e05feb1c6a0" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_2561281024102496_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "1dcbd278480434135fb838ffcdc8e7716e95ea99a1cc36d544096dff9e9aeba0" );
            pt_len = unhexify( src_str, "da3b8c9e4aa8443535b321c3e9bde3c6742cd9f228c971257430b27293ebeb635917d6cba976c81934c3077902911169e8c6197b2d56a046b7ff03b482c38172accac98aacc90076370df28bc8a2044c393c7541b7b69b0fb852746dcf3140ace4e76861975814d2b5966f7714fb6cfe3e4299d79182fc63a345067a0aa54d8b" );
            iv_len = unhexify( iv_str, "b737bcdee4ef83aa83f124cf7208a671" );
            add_len = unhexify( add_str, "49a544aae76b04e62211428a2cc3719e4451f3dbf9a23b6ac824fc472e95e38386d267415c1472a8b0707b0573b9eb2a39a5d5a13464947cc3a7a7dd3b7196f11e87ab5233944f7cea3f4d62b088febf8b82a44d4ca6148be1ba24905432b7ac2bb4ebaf22d3bce97ac2bd34158b6011fbac77ee1fa96ca0c9c9e0207044fbbd" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "061b491b73f9250798a0fb1fdcd72a70eddc9cb48c1f10119387d45c50d5fbb8b85592a7977487e45342fddeb8d481eef3b99463972f66acb38fe04953c223c5f3e02611c8f33cb9ad7466860895fae585d40bc78ec14d1cf17b4c5b75e4d8c6341f1eaf80da4a78aaaa30d3bc8bff15f234aacbee4067a947e42275b12e0bdb" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "b897da3061c77aab5eb54622" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_2561281024102464_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "2e00467f18536ea6b4d582b2480ebee883e4f56bd91af3ad7a47ceea3ece9acc" );
            pt_len = unhexify( src_str, "d5334398318ade59e6bda5cfce8e11b25c9ccefa2f651eb16f66c03d84dcc900dc7c85e6d2b778b155ae4591af0698df7f3b8b9f64d4442ecc82035f7d8e71a5f61c515a963f2fba077f3cb8276e91b31b3f8aa193988a16a86ccaec4a688ad68b5146925ec21d55ded407709d34d140f37e1f87d955619453c3704e83918088" );
            iv_len = unhexify( iv_str, "aa6716e6b7107876a3321d807a810e11" );
            add_len = unhexify( add_str, "5606a0b77cc9020955c7efda33b7080e9c0e9fd374c4201b4324b3e6523b0407171141e8246d01292a34dc69331f7177d6b7238e16e0303e85741f9cea5698e42fc79217d9e141474068d6c192713c04b1ba3573e93480f69e4cbf72090d46d62d5b52e4a7613af8fcf0010d0024ea11c19cb04571c6d7045a1157cf81df18d1" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "249119ace4e292ffdfebb433d5b57fa1518af3389eb832146c3adc2dc62fcc9121d7f6461a53ee107ce7edf362b365d8bc18e50cf9c328cb7c7aa7b4e8bfa07c34dc81c38fe0982bbc3b543485ea4b0ce5a76c988cdfcd241911cd66f5a5f9e0c97332bb0f3926117c0437470717c63957aeba1c55d96b1ff0f4d6045f908cd4" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "70e986fced03ae67" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_2561281024102464_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "a18240f6135e7b6eac071546ee58bb52394bc34ad4e91ee678b72e4514fddcf7" );
            pt_len = unhexify( src_str, "02f288eea5588e7a011f4d91eca232af70f60ae3d9302cae5a8a58798c1b4e973e3b1d07695934ae871201682554ef6a5b94976c6a1aa73d354f1d65e3f025bb2a3f1e93009e822a87590dbfd1965904223049c5ac0da8596955199ff767b92df10d1f9c05c40bd8204846c719c5594000cabd87342f0447e4e466c3788723f8" );
            iv_len = unhexify( iv_str, "149da8186ca73941582532ede16edf3d" );
            add_len = unhexify( add_str, "4d46e1e87322ca84d5bb92d58670f644083db06bdffd99fab0055a62b64a30b5a5673a108f0b9f114d379d3fe63a1f63407881c5b5cb03142109c158af42a00eb24d3b1873edd2284a94a06b79d672bc8f13358f324af2622e9aa0da2b11e33567927e81aea24f3605168e602b532fa2cf9bde5f8cc0b51329e0930cf22e3752" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "36cddac99e2673588ba783d3c085b9935626687a2dbac9ad10deb4867c577d6f80453266b2400afd773e4edeb743c32562e85f7f8f43dfd87b10a2dd79eddf6e580aeb4cea92ac21cf49ca97398cc23c02b0ca59257643fb2bc6462b9cf04658352d53c2ee50d87cc5ca2ecb722d950f0daecfa0b7c33aaa2c91dd8b093916cb" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "73cbe40df3927e80" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_2561281024102464_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "4b64bded6c658090a85b5d889679c6a00579498aa82be1e3a628a1cd001e52a6" );
            pt_len = unhexify( src_str, "182cd59dc1934199d2d2a2712157438c347e286f66b5a2b8b5149aa41ff7ba82adc3751be379741124dfcf05c531416a64f25f0d28abb6f7bf98c80762f0fa363da679437621dcf61bce43ef4d63178779d1a3ebffb82044d427ef522cbd2643cf1f5617a0f23103cd2a164a59f182b151f47b303c4eb7387ee5cb97cabdf985" );
            iv_len = unhexify( iv_str, "99aa6f359534da409a18540d82fb3026" );
            add_len = unhexify( add_str, "f55fd6255d8a188ce9a4a2727699ce16c8bc5c6adba88d94106038b74deb79c9d43bfaa47375148d843a5ce248d70193c8017196941b2d9e2dfd4375a3390c19d2f833b0b265dab30f26adee07ab0aeeb930dc3a9fbcf719a707fac724deb28dee2a6788b17fa3505290c2797c6dbf930b41eca1f6d54d75b820e62ec7023e93" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "5a1211218174e60690334856483a3066e2e8d996fe8ab86d0f8fef09aba9ef0acff9d3e1e5cc27efb5464bc23bea9c778fc74206ae3a16e5fdbf99694ab7096f23c4b395d7a7b8d6675e56b5505ff62f52bf183bcc4433298296e41662d6519d9c1f0a5fb3140376c8890547eae72afe75c338ba97fad9f0184dd311bbdaf3cc" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "8dbdc0746074b486" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_2561281024102432_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "cadef353122cec1fdbc236c0ab195fc4d732655cef444c00b6cba5c61e01c614" );
            pt_len = unhexify( src_str, "a3d5e55fa3110a268cf1414a483adab6d58ec8762a6e6be81269c0369e8840333503bc3688c7be001cdb84d163fa1dfb05f3b01ffff31151f1af780c796822e3d564f785964a546bcc2a320d81a2bc61058652a8594ae9b9b0917400e08d4a99fa161376ac53cba54c92889fd3497e233aff4e12cd85d57375c7c89e92cdf5f5" );
            iv_len = unhexify( iv_str, "d765b5954e5b486885dc78ce6801516e" );
            add_len = unhexify( add_str, "ba0405745971eaec5d337fd22e0ad287551e7084f1c9c38231d675719e3980356e183a99a3c760ecf7a8ede5e0dac8d2bc13e135570ff6e91a854ea3b457263b0e77896fdf7bdf0b53c8276cfd1ea3e8e22450ff2665eacd24e5fb2be89373349fc9e2967763d43cbd7adc9a376b1b4ab956ddf8b1a56d9385fb7e861bc34df7" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "9b99f984ae26f9cad5b3c8058757a0a5caef0fb86b8ecef0c1bca6b99bc72b0d5345a00ae75e37d4e651008bb733105d2172edaaf5bda4ad950a49de55a514e882a470dca7c7bbfddde40d38fef4e1f3864fd7e212bbc0383d0bc29ab2303c8935d49c35d7d73df2fba0daeb5f37f9ab0d541766da71b33da1018a3f287ba312" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "c374cd77" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_2561281024102432_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "0cfc42773fe2d16a59da52234af5015271332344448c214a2b4a0bb53b07a0a0" );
            pt_len = unhexify( src_str, "dfbf9eaa46c368b28ef50227db97f29b5d9ed599760bb83f5d52f92ef5522815d6952ebb0d9b4efe8844216d37510746caf8c775d2c862bad8d67effe109a0cbcdd14ba8e31fa420a475e55ac6b02908346ad1b064d5b6b869503e08d057ae65e9dc2a2a26345917b18d1b715a2372e8e114a071eced0c29cc9966d7205ae010" );
            iv_len = unhexify( iv_str, "45afb3ba2db9287f06cf48405764a955" );
            add_len = unhexify( add_str, "16d3ad553cc0fde3f32112bdb478450c65c854927b198914649a2820a9e3d01131b693765d40bd2bb74a50eb4cd7bc8dd8dbac9c6a61acaf5e4cf81570814b30a6a11877a8f9c5df342f70008cbf0576bd27a50bfaf6e22a40bd77435da16b666a06d172aa981bdcae0d25b8ab002c6c1994a356d3c3b7e4dd7b99892b0784f6" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "e29db2c4bccef2dda828ce652791d424a86cd5790e6ece67bc029ba9520bd8f35a214a73d8b86564df0eccdb60eafee4170da2694eb563e5a854b25d7ba0a4c53465fdc15c6e267be2e54263f97aa3edbe2358f3d9b8d28997388a57aa427a239a74534393593196253de1c2946b7a437a00480ecb2eb08dbe55ca2b3641c36f" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "39e01fa0" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_2561281024102432_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "2a840df4be22c70786c873058d2a6e16dd9895cbfb55b9c9e98f958cfe62e65d" );
            pt_len = unhexify( src_str, "313eddc53f3986927a261f498283b6dc4a39d26f98c7428127237d79a11c5e626e2e9cdb68f72aa3168ab23dfa2f5e03bc65a68d781f23fb9e295909cd9f0f3e5648cf82f3f6b3b509b0a333cb7d9f2b6e444c351a318f8f200a921ccb409def21b87bc55ec211a76a518350e6ee21d7379edd004b3bfd1ce9086b9c66d80ec1" );
            iv_len = unhexify( iv_str, "ebf155f7cf55e6aabdc1171c95c45293" );
            add_len = unhexify( add_str, "8abb8843de1766cfb8d6474496acda2f7a14e78a5e4c787ac89e6bc06cfd42173c35b3a75ddff644f4a58aa7502fedada38a7156457365b4c3c07bc12a8f9061331139b9a2b8d840829b876beb84f27d5a64093c270fe6c310ca3afe987bbc5ec4dc06358d5bf77c7b4e4fe4078c6d3ec28e9a281318da88949c478094c0065b" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "769869a55754eb5d6d42e22a2b5271b38533fc0c79642e250347d34566eeca732e0565f80672054bd10cbd3067730dbc567039c730d8bc32a2bdaad09885651533a4f03174d4e6510547c1e1dd51be6070ab0ca0cceeaccf64a46d0ef87c0311bd09973f3b588a4dfb39c85086ea5d67dc531c287b83c161dcb25e07b671343f" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "c364c089" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612800128_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "461566cac74f9220df97c1ab2f8bb74189a634bc752f7f04526923d30506949c" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "546d821e437371061cf3207f3d866c15" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "44193072791c435d6e8ea7756a0bd7bf" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612800128_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "7736dbb38f1fe351a7fa101d91da62124c22ac02ee06b9413f56691067572f73" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "5f01779e5e4471cd95a591f08445eb5b" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "1a1f08c8f40b93e7b5a63008dff54777" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612800128_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "eedcae924105c86190032650e2d66cf6927dd314de96a339db48e2081d19ad4a" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "a39d400ee763a22d2a97c1983a8a06a6" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "3b4294d34352743c4b48c40794047bea" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612800120_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "714df4b69dc00067c4ab550f37ff72358b0a905dea2c01f00be28cec130313c2" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "c46d63d6fead2cee03bd033fbc2e6478" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "2a0271b0666889d2d0b34e82bf17d8" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612800120_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "454021ece9a87a9543a1626820d39edd1eff3dca38a287d8fb68bd315a7a2677" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "51de54b633a7c9f3b7b2c1e4b47d26a4" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "114708102a434e3a30088b5944c272" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612800120_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "d7e90b539c99e8c2187ed72823258c1149890a69a9c0081ff8c66e1cdea9f2f6" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "6dba3273560f30f118a2e0251f7b7d76" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "5f45e00181cd2d7feb4723e0cdca24" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612800112_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "2948233eec9bf8adf7250b20d62df9219d30e314c5932383203805ff9f3dc5cf" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "d6b8e723272e26922b78756d66e03432" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "14c9a9a217a33d4c0b8e627641fe" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612800112_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "c73fb5e732ebc1dc7c91ac25de0d01d427de12baf05ff251c04d3290d77c34d1" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "c31220835b11d61920ae2c91e335907e" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "9eb18097d3e6b6b7d5e161ae4e96" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612800112_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "a46aff2121825814c603b258f71d47bd9c9d3db4c6fe0f900e0e99d36c8f8d66" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "7cb5550a20d958490739be8a5c72440f" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "8c76eebda0f1fd57f05a62c5f93d" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612800104_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "61a612c76de551f794a146962d913f60fbd4431365b711217aaa4beaa115f726" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "2d25462c90ad9a21073729e5efc99957" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "e4d3b277dc9a107c0392ca1e5b" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612800104_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "4b233480239fabd2035a7c9207a8e1ab2da45a90a472b30848fe4b4757c628db" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "50d45096afd0571e171e1ab1ffb3720f" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "5393bc06b8c5ecef1264fd6084" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612800104_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "dc051ac63e6b051594158399291ed101a3efbb1701b98819c4835a4863734371" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "1f304d4d7f84ab560366215649b0a064" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "1081dda9e0a793916dc82f7848" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_2561280096_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "75f76df772af8e3019a4c1588a7d59925f80ce0d5647030f29548374e7bcc9e8" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "d407264e09fbc853b131c8a9f808f1de" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "d515522db52bb872a4d3f9d1" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_2561280096_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "608d7592c094322b31d4583a430986bdf6aa639cc4b4a0b3903e588b45c38d38" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "6a631952e4990ae6bdd51052eb407168" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "eb8851cfdd4fc841173c4985" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_2561280096_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "86a90631e5341e67dfa55e68b07522507b437fbab7f3e2e26cfc6e89ef9d2410" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "67763ee1890e4bb430ac3c0dbc2af997" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "c6d11901b53cf6b13ac03cc5" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_2561280064_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "b8d12783ba2548b499ea56e77491d2794057e05fd7af7da597241d91d832b33a" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "0365436099fe57b4c027c7e58182e0b9" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "41fc42d8c9999d8c" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_2561280064_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "eb17c1bbcd356070ca58fc3899bb3751eea5b9f3663c8e51d32c1fc3060b7ac2" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "aca76b23575d4ec1a52a3d7214a4da2f" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "fbcfd13a2126b2af" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_2561280064_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "916aea7c3283aadb60908ec747bcf82364c1827ec29bedcbadacbb9b935221c1" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "e4aefe6f81872729ff5a3acf164922aa" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "2035a7ce818b1eb4" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_2561280032_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "47b4b7feb91582a2f6121d12fd465967352e58d9f3d1bf27478da39514510055" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "137bc31639a8a5d6b3c410151078c662" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "822955ba" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_2561280032_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "8955cddce65978bd64ef5228308317a1ba6a9fbb5a80cf5905f3aed03058b797" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "1370e72b56d97b9b9531ec02e2a5a937" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "b2f779e8" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_2561280032_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "7795d631f7e988bf53020d2b4607c04d1fab338a58b09484fe6659c500fd846b" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "f3f5cc7c1ec0b7b113442269e478ed81" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "e4e6dfcc" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612801024128_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "f9aab5d2ea01b9dc35c728ae24e07c54e6d1452e49d9644776f65878199bc5e4" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "96ec2252e51ebfb731b680729be73297" );
            add_len = unhexify( add_str, "983a102a67359f4eecac465b0d65908a487c98c593be89494a39b721728edc991726e1fba49607eed1f8ba75ae9ab82a1a95b65ebdf48d7ee3c4a2b56832f21a483d48c8400dea71537f4c459d1cfcf9d2cc97b32eb7c5146cbf44d7e5ac779e9be0ae758eafff2138d4c5370b8cb62d70ebb713dfd2fd7772fa250590609844" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "766b6dcf491a5836ef90f47ac6ab91ec" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612801024128_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "d713b33af57762f933d6abfecbac7fb0dc1e545dd7c01638b0e1510af719769a" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "5da52833b6fc73c0e4b1403e1c3c10a2" );
            add_len = unhexify( add_str, "374dd4ebdfe74450abe26d9e53556092abe36f47bbb574e8184b4e0f64d16d99eaf0666fa3d9b0723c868cf6f77e641c47ac60f0ee13dd0c1046ef202e652b652f4b5de611989223b0acf1ead9b3537bba17ccf865a4a0fda1a20b00e3c828b9726bbd0b0e92fa8ed970eed50c885e6d69604278375af7b9ae47fbce4fed7d03" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "6151956162348eb397e2b1077b61ee25" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612801024128_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "77a1e4ddfbe77a0ca3513fc654e7c41609cb974a306234add2fc77770a4a9e16" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "30d6ec88433a6bdd7786dc4d3693bde8" );
            add_len = unhexify( add_str, "69beef4dbdcdf4e8eeb9bf8ae6caff8433949afc2ffef777e2b71a99fde974797dfed2254b959430ecc48db72cee16c7ef41fa4165ce4a0636ad4e40875d193a3c6c56a6bca5a55bce3a057a2d3ac223eba76e30e7415f00e6a7643fda9a1bf4d4b96ce597ffe30c3f780dd767cb5681bb7a3fd11668380e272bdd70e66f18b6" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "d4a3c91e02a94fd183cb0c9de241c7d1" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612801024120_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "303930b8ba50f65a50c33eccd879990d5d87b569e46f1a59db54371fcbda7fd6" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "2b2b28d8a5c94b6f7ee50e130268a078" );
            add_len = unhexify( add_str, "c2ff20441d96bae4d2d760dcbae636ca7e01d263c28db5faed201bdb39bcacc82ebdc943968aa0accd920d258709c270df65d46d3f09910d2ea701c018ec9a68af7fb3d76a9b360de266b2ac05e95c538417fec59cec1f07d47c03511751978baebd2e0e4f7483f7351b5e61c2a60138c97b751f6a8c8323970f6be05357aeb2" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "b597491dfe599eaa414b71c54063ed" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612801024120_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "1e3b94f5883239c45ed4df6930c453c9ffd70b1c6cee845bbcfe6f29a762713b" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "61155f27c629dcb6cf49b192b0b505d6" );
            add_len = unhexify( add_str, "5b7482e9b638cb23dba327cc08309bdb40d38100a407c36091457971bad3ab263efa8f36d8d04fdc4dea38369efe7ae5e8b9c190dad2688bda857e48dfd400748a359cfe1b2a3f3d5be7ae0f64a3f44738a7c7cf840a2e6b90ec43f8c9322c60dd91e4f27fa12197fab7ed092990879e964ce014f6be2a1ef70bfefe880a75d5" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "7003f04d6b6d9dc794be27b9c5d5e5" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612801024120_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "9080effb27994ef831689da10600e7a219db93d690647457702c217b08057eb3" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "f45514696ff5ee1e6e5797f7bcff05c0" );
            add_len = unhexify( add_str, "5251f800f7c7106c008c0122971f0070d6325b7343a82fc35f3853d25c878215e7a929bf63cc8996f0ffb817174a351b71d691f23021f58777f962fd1d45ff849e4612e3304ae3303ace7b8ca1a43f54e662071c183a1695873f5567397587283433d1e76cec1103ee76f8e0472814424b8981caea1f624131fb7353afcd2cd2" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "cfb6d9bccf0378fabae08fd230edc1" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612801024112_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "8c291f0ad78908377039f59591d0e305bdc915a3e5bfb0b4364e1af9946339c0" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "a9830d5663418add5f3c0b1140967b06" );
            add_len = unhexify( add_str, "e43c04e1f7304c1d83235120e24429af8dc29dc94399474d06047fd09d61ddc682684776c81ef08d97f06db6e4cfb02daea728ec6ac637e1ecfdb5d48f0440d8d8ffee43146f58a396e5151701b0d61d5f713b2816d3f56d6ee19f038ccc36493d9ad1809a49aa5798e181679d82cba22b0b4e064f56af5ec05c012b132bda87" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "275480889efe55c4b9a08cef720b" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612801024112_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "96c77c11a3336a41b61ffdc1724a80735bbe91dd4c741fdbcc36e21c53335852" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "655502d70119326405d8cc0a2c7a572c" );
            add_len = unhexify( add_str, "c01034fc6b7708128fbf4d6ffa4b4b280a1493b9e1dd07079f509479b365f55ae9290689f1c4bdfa439344e3abb17f3fd3d5e2f8b317517747714a82f0a9ace04938591d3ade6d6095491a440322d347e8634008cc4fd8add7c1c4764afdb2b098b3f5604e449e8049a46b6192647d19cf88fa5ed1abab7f313b4285560cba44" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "b4d581464c4bb23433699c418ddc" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612801024112_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "e2a3957393669278f052ff2df4e658e17f2fe32811e32b3f62a31a3938930764" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "a6f5a1f1f1ac77a1cb010d2dd4325cbe" );
            add_len = unhexify( add_str, "ce9c268429ca9c35c958ca3e81935ec60166aea0be15975baf69103251efafd54cbcc0bed76a8b44a5b947199cd3c2dee6878dd14a5a491a4a3d45788405d0129354e59c047b5367f1158bcf4e066a276951d2586bafc3c11f8a982ca7c3ba4677a938498bd51171552ea032fe1bd85cfeaeb87e87168f7a28e979b08358f841" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "cd5986df8e9761d52cb578e96b1b" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612801024104_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "2b17652f7f04073afe9d9eb8b2615c7550968b9776b139fcc4f9b0300912cbdb" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "9a8ac23ea74b292b7386138666a0fb60" );
            add_len = unhexify( add_str, "2732107241e6136f1dd28d233373079d75d6ac13828ae7afc751b6f9c57e77268c52ae91f4ab3016af2764597994573cd6b41f72e21b60ffbb3aafc9487ac19d0ffe8db2ae2c7505ae5963b032d1ee1bffb4c5bd88bb0c9a350ba26ee3eb8dc0a157955333e4f28c5ec7349c39229dff9f440da72909f2870aea873a76545ee8" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "f7b94229439088142619a1a6bc" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612801024104_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "16fe502e20d6473ed9a27569b63a768ecd428738904cf0b337df510775804619" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "431a8d78b91414737e7c6188328a6d37" );
            add_len = unhexify( add_str, "934bcacbac10ea4ff6ee94b17bd7379b88489fbf123bf496c78c9b6b02ee97dd62eedd05b8f44f4912764920129e711701628991a0009ebc7017a1a19b177ec9bc3b0f280eeefadfa310708dfe214428a184147b4523e66f2d62630d4a12fd3e366d27c3b7d1566553c9b434ed193db083160da1f241de190bcbd36f435e30f4" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "1dd3e6d610f359cc4e98d36244" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612801024104_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "ccc545fd330cf17e27d75582db28807ec972b897f812d6ed4726d2a18daac76a" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "caf2f56584a59c42a51fdbfe4ad78f3c" );
            add_len = unhexify( add_str, "e85ae6b27778893f36f130694af0b40f62a05aa386b30fc415e292761cab36fdc39bf5687a513e25ed149414f059e706d8a719b7165044fcbd48c773eae546380b8e667b56824e23685173ad9015a9449bc1cd0b767981efe09da43a07bf1aeee08ba05d387b8a00199e18c874fb3a91f77ba448c3bff971593f94747fce9cbd" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "5cf5c7ca6fbfee63854f3bcd15" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_2561280102496_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "8340d604770c778ee83d0fdd5703b1fb304c3bffeb6f4c65e2dd0e12c19bddcc" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "c0a580465b1b2e8344f795a6578a5151" );
            add_len = unhexify( add_str, "799f228962ef87865dfcfa0addde7366de2e4aa78029dbc8d57d7e50fa7c74343458df3465103556a3bfc5ce217fbbb5b2835c9f76b70240b40fd605bcfa6b790d5985a8ba54354e0625263c628e8746c451504fc58a179f90f77f2b293d8dbf5582b031082025c806e60143da9ebb6133ac8367376d0572b32569ee799540ae" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "318f56bd0f3832d043ef700a" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_2561280102496_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "74de45262fe09e12c9ee7100030352112a6532d1874cc6792b4da6950677eb2a" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "9f7fc7367f9afdb67fd1afffac058e2a" );
            add_len = unhexify( add_str, "289ac6f5beecbbcbde5cb3b0fdf4a27ba237fca33719f774ed33a5fd35d7e49f76d3e88c53fd35561655c35469f3eefb5b2f776ff2799aab346522d3f003154e53f4ef075f016aaa500c76870e6659a5f9af197c9a8f5b9e0416ed894e868463cc4386a7442bb0c089a9ab84981313c01fec4fc0ba35829b3cf49c6447f56a4b" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "bc1b8b94ff478d9e197551cd" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_2561280102496_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "441ec8afce630805d0ce98b200e59f5656a5ce19e5ef58241e6ef16cac7646b9" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "a1cbeffaf55708c375dcfeb496b21f4e" );
            add_len = unhexify( add_str, "5a6ba5d3f5a7a4b317c6c716564c648f0e6bc6b0f9a4c27affca6d5af04b7b13d989b7a2cb42ce8eedd710be70c04c0e40977ca1c2f536aa70677038e737064fb0e23d3dd48bc00ebdd7f988f57141e164e3c18db81e9565a62e28c73770666ff3bfd725eebd98946fed02f31d500b0b7ab4dafeb14e8cc85731a87f50d95fae" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "aa4bb3d555dabaaeb4d81fcd" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_2561280102464_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "d643111c973ffb7f56bfbf394eedac54be2c556963b181cf661ba144f7893a62" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "4575b00b9af2195a0cc75855d396e4e8" );
            add_len = unhexify( add_str, "b2c53efe59c84c651979bcc1bc76b0bbf5e52b5c3115849abdbc469a063e2b1699bd292e5fcb3476e849c9edbe6ea14c2ab948ed7d21a21f69406621d3d412b043eaf813be722d92739a33a361ed8081c0eb00400c3c7d4e329f5ba4f7b75d534500f42f178048cf2e95b768ffed79c350f2ff72cb355abdb30af0a1363c0b4a" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "9d1d182630d7aeee" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_2561280102464_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "91301ee0ca694ae6971ee705f53c7ec467f4c88257d6466f6f8159a8970384b9" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "345fb57e88124a414828730a85f57871" );
            add_len = unhexify( add_str, "c13623824a204385f352388098f5e2db23426f00a73c60c1bf1047ce2c7cdf7f7cc8475781fe7075d1226ad18871e12f0156f35e6ce7032efe3bade1c807f9eedc720fff7a27a2f4690f904be9c99b54a65509eab60e97c4283596eeefa2b2517e95de7620382e3f780efa1dbf5d3908373adfe784a4faf298681e171bade4b3" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "325d08c5b96068c1" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_2561280102464_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "b6ba5c11daed7f868da9bfd7754d555a147a1ffd98c940c1cd5d136680e05c10" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "b0c92b79d78547496d770678e1ce1552" );
            add_len = unhexify( add_str, "5b1ac8ff687f6fd2429dc90a8913f5826d143a16a372cca787845cea86d9b4778708bc0aa538f98e1031850f7c1d97fb64fe29adce6e1d51ca7f5203fc0358fe0bc54347e777dddfe04e3d7a66a1d1e2bdb8b8929e2100daf073845db5dc0b243819754c4c08f4fc3631d1cbd79ac7604746d677ff035930fcd6bd652e7864db" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "b1819b6f2d788616" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_2561280102432_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "5fcae1759209e784dae5a8278b267c414a03ce7c803df1db7815b2910d10ce19" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "24c5c349b3effebfd076c88a591b8301" );
            add_len = unhexify( add_str, "ca2778e39fffce7fbe8f912e69d55931848dd5ab0d1bd32e7b94af453251a47f5408ebacd7b50ddd1103fab1c72acc0a02f404c5661d8450746d781e2c0861b6974ade9ee2515da88b470f16d5f06007f35ce97cfc17fd015e438af39ca6127db240babe9c42ed5717715f14e72f0ef6ff4ce512de95a179e60d6393e73f216a" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "8e59f30b" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_2561280102432_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "8d71a70fd58125b0da8dddf8d23ddbe0bc44743753bdf259448d58aae54775a6" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "d15b02572dec98398ba9e68e1a463738" );
            add_len = unhexify( add_str, "81313be1eda9f27e01b30877ca90e825f55ef60b15548c45c786c44b024e7198f333be7ddd2c3f593a9b77b68e6a7ac4cfc015aeec66f4823d9be7152f02a533f375554309a4db0fea8e76255144458e488fd19106d9a9614e828ae306fe82af89e7981369b2259c49bae77f8ec2b1f169ef0449ad083d11907234b72ed2e464" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "99df1b8d" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_2561280102432_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "b52398c7c75e1b146cc9998eb203159925cf6fc0b1c993ba46528e2f8e8087f0" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "afc9a60ab8448b77fb05e8410d0a26e8" );
            add_len = unhexify( add_str, "770b3782f0e3a19d7d6bb98fa3eb0b916928a2970701c0f4a372a0ecd63499444ae02fd269ddb7d92e11a9e11d0e0b8bc60096a4be79a1e063174b710c5d739d8d05ab5c8ba119ff40843cf8c5dc4e1bd6fcad8389de3b606284c902422108d85eb3589524776641b175946c9ade1465e0d1064c5ae073be90e3261878a9af98" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "32d6b756" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810240128_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "6793869513ac886ed66e5897bcfa263877d8465fc762b1ed929ba3d08615fdd5" );
            pt_len = unhexify( src_str, "cda45e29f487f21b820e1af2c8e6d34a8bdf3f72d564a4625a6e06f9bae1c2eac3bbd5c5958fd75cf389a1a31391211745029dcd4cb2575f40ab04710a909b88c2d430cdee279f54cf7c0ff6638d1e0e631f526ee198cfd6e5cdf73d1a11b69de01d640f385fd829616cd2c0e78f09b5f64012e42dee9eb0245b72aba1404e0c" );
            iv_len = unhexify( iv_str, "a43de15dae25c606da1e7a4152f0df71" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "385834c853772af70675b6be2d5087df84f88b6a303ea594a170e6dd0398ae270fcec61661ca373f4653d8dcc9e71767568c0fb03023b163bdc9ae8a08ea858cbb03b8182b4674147cb35ffda14a2f50ed9eb48d5351f00eb2fa433fdfed6f94833bcf656a7e350eb978a0aaf7a91674145f28f64693197a116b21328e273dca" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "159ffdb05615941e11f0db46ac8f23de" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810240128_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "9f77c141b234907b38fb45f1b3602f3c29de1ed839bb7ba51f6192aa8baaa287" );
            pt_len = unhexify( src_str, "96dcb74a78e99676a71673e3c9f94c34b34dad2748a6e42cc70ea50e41ef8b86b5992295d2cbc8d621fefce09e8948de7e696b9788377d598796afd002a82b628d9890db78359e1edc075cbc0d3f11d544bfdf5c8a838390cb856735942dff260189c00accfabf720e5fef1d9b7131a6b2b769f67374602d1a7ed9b899b2c398" );
            iv_len = unhexify( iv_str, "1b49005788148665cef20d8dcde41889" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "b4ca59caaa94749317789b92257f2ef1dd3d9b1f4ee9540927a6ae7bf5bb0b348fcf25ba8ddda79a89d3174ac1713421291910c8926cfbb4ec1e59be7dd50e816ff586f165c605371ee6077ba4ac0ce10499f9a2a44866ce6319fce22652226164cc0a813c3147c4461dd0410e3701d4647d5a003090082e367cb9249cf1be47" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "8048ae0c35a656fcaa2f4c1b6be250e2" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810240128_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "2419fd9dbe58655122ac1022956a023446b7f4756163769fc1b99eaf8fba1474" );
            pt_len = unhexify( src_str, "93bc33dc647c7321152b12303f38937bd191ab3ce3b3a43a29f6853b33e415667d97192fcab2d1baa017042b301d03bae2f657505cc58e3aa4bd849d1ce85ede0e192a373a3894c41c54edbae29a209e16c87c81445d43968595297b50b55659f8b92d7282a2b3ca85e4b5d4ac4ff5062635103f2c7806fcc7378d5c2013be72" );
            iv_len = unhexify( iv_str, "94ef13dbfe9f362da35209f6d62b38a4" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "3db23c161cf352ba267dab6a55f611eb5fff78a75288779a167cd0e4db6e75d21f11f4ff2928abcb1b46d82c2a0b1f647c60da61f9a72565f629b06a7b3fe96e4141a6886436859f610724bbe43fb99fac9b78b1e0138e2d57ce5fcfac1599bdba5701cb424535fad9ac482ab381eadca074e7376101b4b436f9c43ed760a0a6" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "ecd4a7370096dc781c3eb3f7e5985ef1" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810240120_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "08e11a8b4b24e63060c5002713725bb5b4a412f1d76eac13989738ce94e19642" );
            pt_len = unhexify( src_str, "d5598f4e37274f3b617aa4f9cf6b8547b4eb1e0eac79f6eedd6cd5364f8891f66b8d0cb09f54777d461bbf92d6fd74b3fac412b77f2c48e1024cf09b83c1e71bb86f0a20f82d296883ffee62a4a192b184bc6d7ba0448c1519310c83b18c00e71153137afad14f096b43d454f205ba6b6c2ec162aa992cebf50735dd9bb37c7c" );
            iv_len = unhexify( iv_str, "c6f1e6a39cabda1089048b536e39cf67" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "1fdaf0156456b6b2a68d66091bf2260792748acf3e7bbb7906af8e0df3b569a7c03ee3a48bdfdff7ccd52433d0bbe8c5fe30d93633bb9d591dfad7d81bf8efd4d4a3c5c0bf2ac9832f0a8687f16be640fcf9b19169c251f46b97167d95115acdee3d4443df416275f5597a52c17a4b8c4b723d4b35a7fd0b380fdebd44df8bd5" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "cb9f4d4610c67acfe612af5508bb8c" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810240120_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "da2dae0107c284ec2aaf6e7306959df1e92d3932b88954f119ab677c6b9dcdb5" );
            pt_len = unhexify( src_str, "277675044caf1713109d4d3abf50c6fb67dc67f7fa584fb1a41c833feead03177cf4b42edac139807ede16eb1d9bed27db741f9542d437781405608de18418c9f7269ab3fd88f6a922a31eab5a3b8b2aa75ee4315fcea80c4954ea6613b1360b1c7c6b6da815e3f6e50f72b7e69c3b6cb3d154855e3f83cbd1947eb54018155a" );
            iv_len = unhexify( iv_str, "2005f79d55b12e6dfbab7fedecc50e2d" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "c2aaab524d1738b5244af642bbd16b32ba954e69ae51acc804a6b0f89f6cb77ba2db2b0e109cda6036786f9cec5587b01e306ee8b3d588748c61ad7fce1266165729d0153ee189746b107ce15ced667279a484294725e120dc1803d2c751784436ab8ff1d5a537628ee35742d1917dc51f8cb46c2d6b983bdec502e99b85e5b5" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "52b4d7f2cc44f0725ee903551f681d" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810240120_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "637807b3e472e2287b09d5a3ee62f791a416419ba35e11c49b24dbadc209f0ba" );
            pt_len = unhexify( src_str, "e91a0a7320329dabb0d0fd7f099a4d313724aeeebcffe6fcea5b00af27d258cf9774845d29aaf5dad634c6f087c3311b1c92775fda8df8820c91186da30dc79747be6ec6230f2c261063143f4fc89d94c7efc145e68bfdbd58fb14e856578ed57ee5b3cba2cc67dd6497f05d1570efa496b46f5bcbf82ff9c6a414f76fcf3f5c" );
            iv_len = unhexify( iv_str, "46909d8dba6c82b86c7a2aca3c9e71e0" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "13b4ad9c51063a7f697f3fc68030144aee0aeef0b5a52c9d4920a7185b0452159cf13e64ca216ff16637d0946a75fb5da283fcd263dd7ef2c8f14cf75537742d1f0e48846fcdbf03bc343203f7c31cf61b36374033462a7b813f4dbe9386e57874591fde606fbc150d4916c339f1950b09b1911b1b9119c3ff4053e05910ffb2" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "6a5c83f807401d1a9a3a2688289f61" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810240112_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "33613dc6e029df0f3ab9ca66fa96cdeaa84c1261dd586723b1ce873545565f7a" );
            pt_len = unhexify( src_str, "775862b39c2a509afd3470a56891fbb79bdb7dacfdb9ac72ba4730cb936d364e1aed3c92c01a018cfcd7953f751003934c15bdfdf2826e9947ea8e521f55fd2a04c75156e4910f38932c9732eb3e60423e849d34c55e3fd00b48d83028e3b4f35686016126ff16c942ec859d3c3aa2ee6d322a92dc9fa9b0247423416f5a4b47" );
            iv_len = unhexify( iv_str, "59484fbc27cdbd917bb55f815f9faab6" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "069f80826dbee03e6a3437e7c6d16eb6022bd14827b8e45bd440d9b1a8ddae09999388ba0b1be0a6bafdb96f26dad523a3592fa610d5091f68380f4c1c3fa9ef7a0796ab183e8a82c2bf1f76300f98ce983eab7a93ddb18f1c10534fdb61ace83cae37e225930ab870a46285e733788e907255ca391945d409d2e53dd8a28390" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "9f31f8f8459eb03dc3654caba5c2" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810240112_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "75d8132f70ef3f2d8946d296c83014683eb2a4a58b555c0f48e4bfa5774d6672" );
            pt_len = unhexify( src_str, "a5be88fd43dc761838f3a9c7d62923c38414fa61b3678313cbc8fa9c2e5effb6cad7d5be5f39a71a28ff327b68a69f7e6a6bcb90eccacaf3a8659aeb905dd3e38efe57f2bd0d19daacae238baa01a7051084da6598fc5a3783a18decefc8efc8d46c7b1887f87d6d70c909df49340bcc680832faac3dd23cab5bcd80553dd485" );
            iv_len = unhexify( iv_str, "5ff41f3e75c25cedda1b08a41b89c4b4" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "959396b86913337f2b1fb19767b787c18f00661c5d601bc65e884e15ac8043081459e889453e906ee267cb5d04fbaf250144a56c820eca34469967c73daf50796184ecf74f3c054bfa63bdd0c32425a8e10546ac342bb8e38a186e42a403cb80110aefd5f2d0bcdd353daa4430b8e7ec2134925c454745e2f708cd0b90d9d672" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "ca0889a0eb12995079cf9ba77019" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810240112_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "8d44344d2ff9a02b1c75785bc84f16e4d23614bf43b2b9a87798b418e905c532" );
            pt_len = unhexify( src_str, "e5689cef9f8258a748a615070fcbf40ed0b24c077e2f9a362cb536737ffbc5383bcafed278d4c5e0f3c83fdd5cde79483c2c178f6fef05ab50f2b8db680027a175bc6d702d249efcd6cbc425b736f1905307c9303a4bd8aca620b57e3bb4b68f2a515259b06cf5365b675edff3457e2e915d7da1e0802f7300b3d56c4644f4ad" );
            iv_len = unhexify( iv_str, "256a983cd6d6eb4e80b5c1d1cd2a9f21" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "13eeadbecc4c9991e2aa0b1ca819572ef28517528320db970739a16994f82cd8b5bb53d889f298f65c63dcc07089dbf7e9d00612d2cc8220b5630ca0262a698836d906256896eea446f6de4506e558b4f20950528c8c397b6b5b04890204b77a163e46c80c96b3e268fd2754e0380e7330782d606c771d6085b34200a80335f0" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "b33ab1e4029998e2566583dd550d" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810240104_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "3999a6a394943be3d6e5732af5faf26caf483a3fd42c13b7f4f02132e93a990d" );
            pt_len = unhexify( src_str, "8907e8832553264d7e92afa1595842ac661ddfec3f4294567faa0af61b3d0fdf76a922a2f3affb36b3b3b97f18d5172aec0b8f6f01239bb750c0fdd5da1e1244473cdfade83797037ca46d83123e6105c5c54071971f190da0c59821b0bf87242502bd19d19c7f463145bab0e687a18ffb2216c4a2ad2caf9488801c33c78c03" );
            iv_len = unhexify( iv_str, "76e2a5141d094b3a77765ba328f33576" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "995189a396486b451db0167cf6990557287074def46eef872e6cfe1a297e256bdff2b71668ff0184eedf00ff1a3ec91358874718f0af88acf2bdb191e97332dc544d940412363840d4c03c7b2231852393c62d625093011ef314e4f755b1d0ee37690b4dfb55194a1465714cc3cbcdf93af39e666be0407508b8764f7ee95d3c" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "87c8f61f459fd4a09d9ee8b331" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810240104_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "4359a62d54c43770c3a0d51da25cc32fd985d9b41c282887299d2e348aa25a36" );
            pt_len = unhexify( src_str, "f020c9cafba399009bd920c3ffc165d4db47a9ee15ca8c1f51c65e306ccccd3f1d694071a3c765b5255eba6ef6a280f6095f8c195ebdfbee6968b57366e62e16d05b1768825ab7fe66300941270aa121b4fc02ab970ca6e32170cdbccb46fc548620fa1777049343b1600bfb1bdecec6682f0aa7244a0852adbc7aacedfba446" );
            iv_len = unhexify( iv_str, "5fefa85c958417b6bc8a61b5496fea93" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "3b8f829aa1cc1532a434bfbbd25f42480311657215946b9216846704fd5da5e886ca9d130df466c3b58f5259102ea6b9ad756e9f484a38dd0ed289fea083ab99fefbc2747100071744f10e362351d4ffac6c7c1f5a49ef3c78e2dc667f6b3bfd0fec454c4e3139443da71e514540d7a228db193a4c35d639ec13c1198ee7f81e" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "591db861b9060869edb228a324" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810240104_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "0d798a357de5a686d06c329e451d7384bfbd462063fb8ea7d77a13dfa1f2aac2" );
            pt_len = unhexify( src_str, "d920785bd7d7b1a2c9c20139380a6ac5f27a11b614ae110da14203146c2615d81e97649e95edb0eda71a0fa1589244ed42fd9449962a92942e38001ac64b212c7e06c113129712a01556577ae02325a26eb92581c0a690a894225e83ff1e36776f22b600508d6d96a0d1c55316b518df8d09769df5e8340cbeabaa0bf7752870" );
            iv_len = unhexify( iv_str, "50a003c0cb50ae8a3183cd640ea4c6f6" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "9af6a5341cde4b7e1b88346ec481024b40ad95a51533cdd8e09e4809a20684f18eaf243e1df56f02ace9667264cc1c6af6b0914f154b332234f6468cc471ecb2078a9f81c17f4ade83d326b670795458d110e4c4b4cd7fe7f9f5f4d4fb23a038969e4ff4f74839b1edc270fc81fcdc8a0b15b9c2f0561567c471b783b4322ebf" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "6c2f01264f9dbf29962122daff" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_2561281024096_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "29b01b6d15f6e68fc2e7079429dde5363888a6410191d603941bed272daef7ed" );
            pt_len = unhexify( src_str, "123b6da306978f745d1dd86d7df32d9421523a7f329dd29ad98d2c309145844010295ef443a18d37ffe093080682fb96ba9c2c92105d35d77897b589e2abc7269aba8752c2a48c843bebad2c0fa281015ba85f5f709f6aee9b1d49236d5695f7f7d01554b193c89adcd1a91749138952cb3f0ec8b5f046328b3113aaa0715ef4" );
            iv_len = unhexify( iv_str, "cb4ac8373bcbf1b14cf2a6a6a16a422a" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "caf71e09395d596d5a7b091c9e87ba6d522e974451e41f33f3e7ded554f24daa9da719e87793424eca9a3eb3972983354041091ba4b16c5c8c14913e1f6cbda09779188e9b5512917a0adf4b4344f119736ba6328897726a317989cddc66f16bab64707564bb0064fe6ab7b2b5cce143e94d4b6d739f58c47b6d4850697f8101" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "f635ff3d8bfbfb49694e05ec" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_2561281024096_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "f96d8cdcc21884e050f762c049930d78360b56cef5b99ae232c9a8c6e8fa89f7" );
            pt_len = unhexify( src_str, "9cf05e5065531d2539d92ae76a43da1fa3614ffa4b1c73ddc2358f8d71345c01260060239edf629efc3650e0d13174af4294b6da0f39cc7fbecfa324afff89dd7d203416bd144c5e03df60a287fd4a8d54ef9b4b44b3d6de1d9de07418b8a34ec5c28cec3c5b2fb861583178a68ea0af89f2dfbfbd86f7cf1e572e1c8d4b0675" );
            iv_len = unhexify( iv_str, "5a7eb964b6bc9e75450b721b4d1f8f92" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "566abaa23b8d464d6f107699453740e9e189254145c5132fe46989a6654de297398913daacb4083b29f7b31832079616e9a43c9c2878df1df451e49f1e629c8b9de2fb0e4ae9df48e3e8880f3f1ff5ace8842d2695e702dd1b7bfa7c25b0539b8c80d31ac91856796beced082c213e8be56efd646dae932f5bf503af46f491d8" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "c049cce29c401d3d198773b6" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_2561281024096_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "253234c3dc9cb3d50a80598c5cde0e37b6b13bf834f3595a9458dee698a6d19b" );
            pt_len = unhexify( src_str, "686ad2740bdad507ebe97aa5bdbef25b8b030c4cdcaccb0d3b675ca91279db3ea75aa222c0ae98f86c24b10038cbb4fe9f897e1145b2f58cd3e9120f9a5620f38aa1e1f63906f557ff4a4c3223f5bb13dca34f8a1c6419e24ea57d114c62fec6fb9eee58a16b9e6a6bd930aa6fedcfc591311250e7167d43cca5916d5beead27" );
            iv_len = unhexify( iv_str, "9d156414acb63d11cb34870b937c837d" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "96abd56d2f8aefe6c687f035df46c3f952a9933b8a51698e47d973b7d47c65ca3ba2474cb419c84a4c3cefb49e78cee1443a8fbbdaaecf73e9059ef34ac5a0df3fc152ecde2286da8840ad4617fd6ebc1e126314204bdc0a17b958430eb9f727498ff1db17aabbdaf43acca0945342d2ba9346da5373b2372b3081605e895c99" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "3d998e5be9df433da001a686" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_2561281024064_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "1054d48d52693d2797c80d3f10509d1c808f36a4d65e8fd968e5d56239f856bc" );
            pt_len = unhexify( src_str, "a708e9d2d27ed4228e5b23d358561a77d684d855db9827be2bc102f2278f1961d3f056fb76f76204b2c96b916eb5e407f98e58edfed06de2388521832d97211d851d3e29658df738e3a15593b9db016d9e46fe9df98ce972d59f7058d484886ffaec7b9fd973c55644831241c1ce85bb478e83ccefd26b9718bfe910ac311ecc" );
            iv_len = unhexify( iv_str, "87611b936873b63abeaea990d6637a22" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "94473e84659bc18eddcebe3112f55426f48ca4d670291fdedd42cc15a7415aa6795fb75b39434884eb266677e1fa7f530c6f3aaa733c0d9c06291bd7dff4c4e5857b2ee9e9f1f61a85571ad32dc9a3259017abe9eb5111e56df2913535669f3b2d722bd35fcdbd6541918885d9677cccaa902b9d3599cd4f0df1f35f4d11b8cf" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "9bd7cfe1023448ac" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_2561281024064_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "a95dc5127b9cb1c82d558d5b24ae049e24447fd676a49350089951afe01dc797" );
            pt_len = unhexify( src_str, "45f81fa4780a256c40a0efec9547310406904d8991bcf964aa35ec9af457e2a642c1343827839f1f4b42f2b226da351731f416a4b4151f07927c278b371404f027bb2058e1765b367f5433a43fa4153883351041db3f066ef284a3eabd584d1d0b1d594b4ce7b5bca1708fbc661d95a9ac0d77dc29547f022eedc582fc7158c3" );
            iv_len = unhexify( iv_str, "0b177d01993ec726fff082ec88c64a31" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "16c77b7f541d2dc4e8d31da23e04f18f4254aa283e8cee5b776f3d9a27584f459d0747955efff8945f807209ddaa6421846647d4198534b244498fe13a9073d372171d1b2fc38af66204f3de04000c093ebe659173b8d78dcfb8ca9003d2cd44ed168e6aaf55a06f29e83ceb32b98bafb59f109599f88b5c0f0557bd2b28f03f" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "19eb5f808d65989d" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_2561281024064_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "53d6393dd7ecc40f2d52460ecdb0607133ad843ef53f380cd3a2755bfa567abe" );
            pt_len = unhexify( src_str, "72199c54dd5efb28c104e3b7210855506f6577d15c4eccdaa6a621a572e15f5845d648cf71b9fafef3411f6c1a664c7974fe71126a5cbab907e2caa342d8d7a05bc68a72c824896ec40e520e90b704dea441d22c5918f98803a88293384f64f92f11650c2cf4d3b062d30e14d149160742f59a473faf8fe00f4bdab9128c3281" );
            iv_len = unhexify( iv_str, "db7e93da21f0c9840c54c56e9c6ceba3" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "5e83f559fa54926b731334f815783914530bbcc472d4bbd5e65908fb1c421442cb4c57329f2e4ba3d146a6499f34d8f1ec6d43e0cf98bdba923f404b914700edb235b08b0330097ea4162fd0baa1b7177ef0b29d5a6689bc56b8f975d6b6067ade4b8baf1d47a2eeb5b2ed28ebeded381d55d280cb2fb65ce4d82b69cce0594d" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "4e65dde857a0f5c7" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_2561281024032_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "aa4a53c7764a254b06e1d8003810300b70f5729306effba9fb6210f97648a499" );
            pt_len = unhexify( src_str, "19f3a8c298478d6868bf3b31785eb62e844c37200672e6ef1ecc05c616d981e02c333dbc3f86dbb7ab9ba40e9e57e133e6d1d595fcc6d8e9886a84517212669d5d7ce0f1383cb58681b92dc180c06caa1a7ac1ec974dcd7f2bca7ad2ab2789c9a3a487d64c484319bffa56d854a6d40c62b02d0c7898f641f106ff50d22a12e7" );
            iv_len = unhexify( iv_str, "c32288f97af9b6e31aa7e40d9ef8d016" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "1fa6aec7a28767c8961363dc4264e6ab97014264f6fe1dda7e9db8646ce9a5463f69e91aad2fce696f9b641d75635bfb0f97ed2d7beaca944cf8bd9dbfffe77b5ae9fd032575e5333c7ce27538c609922843de87b960ebca7c2a2ef9702dd0c32f787b4d7df248fdf526d594a90bad0d6a8dffe212246c36db71e2d348326624" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "1699444e" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_2561281024032_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "f420b6ef96d9bfe46dcf18246ee230790a6fc854e730f1dd2d1ffd0e8b5c4776" );
            pt_len = unhexify( src_str, "658a954d6c61d0d6f0e81a3c1cc65684483fdc95f280b6d4c964358596c25ca41c389932d74a1a3a17d041e89b7110ea315fadb3128c2c469c350bf9b4723aa9c8abd9065ebbd12c317bfb7090f09633f8c1184f0c4fbe10f5486dbfb847536c886f7d144ed07272a7e62fb523a04111e5ea9e1ab415fd17e72143006db14e9e" );
            iv_len = unhexify( iv_str, "4982f502a37eea8bcf316ced466c9fb1" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "8630aa78aabe35d9360a44bb2094209b6f70d46d71e3949803cf54e33dafd54c6e49eda9e26dc5c0c1e34908f5281c8cb2a1aeee81186cf45d3eb22f486320c7ee0fb7bf3c211b232a8426e7e82f3e05881bf7d9454cddec7f28e5358cd0e9ea2e9cff938be044c1b21911d50b2ae23ab1aef377511ea657adcb560c34209f8b" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "3aa91b73" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_2561281024032_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "50f3b822dfc70382d8695811e6b0a2896ea2bcd4d5268778cd484053c8a19288" );
            pt_len = unhexify( src_str, "15bfb3a562ced63c92561a78374af40c88a08ce02392419e03d7543365c5b6525951ef2dec5927474a0ef85f519e5ef795881db3eafa765ec38e6be7b565a878c13d90c02889dc50cbe87081d9225a515504c7be15bf97f5d72a4d81f218a148a46fbd42983ab002fce0a54719bfe301bb761753cb330dc25be517b87d0428d9" );
            iv_len = unhexify( iv_str, "980810c11abd3aff43408ec9a69abcb3" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "12632296f27eb2439009f6032a3f648370303dcebaac311b684de2496f399b271347b19e045c1060802f3f742b6c780d20b9d589cc082d7d0d580dfb7231171cfb612227fcdee7feae4f8defd34c89fb0d68570e782192a7bdd9a5464f35dc6a4282cf9cc3fdfac988d129eddf8e0795ccc24a113f872ada88834c974df8bc69" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "32c1c4c5" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810241024128_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "29072ab5bad2c1425ca8dd0ae56f27e93f8d26b320b08f77b8bd3fa9d03edc6c" );
            pt_len = unhexify( src_str, "3c7afc5cfc5a1e141587e93fef8427d4f21d892b983b7c9b6e9de3ee168837a1533847c8a2e2ab0706ac1474e9aa54ab57e7860bca9ebb83bd6d3ae26ca5387abdb9a60c4a9928484742a91294b13ab8f51eb4f599a30e9cb1894aca32a62a4c2793ee6793df473f43234c9eafb44d585a7d92a50aebef80c73c86ef67f5b5a4" );
            iv_len = unhexify( iv_str, "0201edf80475d2f969a90848f639528c" );
            add_len = unhexify( add_str, "4c8ff3edeaa68e47bbc8724b37822216d42e2669ca127da14b7b488fde31a49c7d357fb9aecc1991b3c6f63a4ce43959a22de70545e6aee8674d812ecaaef93ad03b5d4c99bdef6d52f21fc7fdbeb1c5629a76df59620aaefda81a8e73cebe4c646beffd7f4a98a5283cc7bc5e78b2a70f43e0cab0b7772e03a5f048ec75081a" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "f3755aae6813e4e4b84a089ca1496564676655ba3c94e59c5f682adbbfed21e76aed0db78390258cf5fbf15f06c6b6468414cb6493c8b9b953b4954ecaf07ecaf8586ae001710d4069da6d21810bcdcbb831f7041cdbb984b7c55878598a6658883178dcc0fa03394519b8b9c3bed0e5c073429f5dd071a9184b015cbbbc62e1" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "0549dd9f2a123bd6d58e5cd16c0624a1" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810241024128_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "aa9999af53720d0c1288fd3fe307a471160635287eebf41dd77c82d1f9cc9d61" );
            pt_len = unhexify( src_str, "6ce6f2dc202750219e15a24e1ff0678ffdde55b27cdcab6da188bd5235a3bdc677f72f106579d02c2970d4542e4e2372886e1a6d74c596ce735f51f2ee6aff4d62bd24112ec7cd1adc7c660561f163170cdf047c241c53b8a5b2e03fde48c249a319bb90c2693c468c9dd136e94e05f067cd1d68244ce50be318ae0464b79acd" );
            iv_len = unhexify( iv_str, "6299d651a032bdf3a7e6b25ace660e30" );
            add_len = unhexify( add_str, "afab0a3d1960ac973ee2f4461dacd10d189412b37e572cad7888bb4d2453f1eefbd6725aadd5f982393dfa59c3cf1ee342dd91e1fbfab10a802e3a0eda226fde2686e7db1015405a3d33c921e5aa857bfda53ca3aed3ff0e18c289406740a7c5d9f86ce43db40c9032e98ab126c7c0364e2efc008312b7641d36503d183fa5a5" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "a8059fe6ff711616afb591b5e5de497b3b7813f9de658c7b47cc3e7b07d0805c1ba05856d98341869b8394f3b5df2876ae19837edb3931eebeb0f26eb6c4a2ea78003d82a98111305208ccaceaf77e5d71996cca4f9a5eb712dd916b71455f741ec2dde51f56828667b7a2da015e1886fba71e496a542d94a38efbcb5353fb89" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "2ff4d8d00400ad63a6ae7842eefb16eb" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810241024128_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "31721e5e3a748a7f7369f3dffc1cbb570ceac868ef9d1f29b944b7e86a26d273" );
            pt_len = unhexify( src_str, "6afc1d22233a60c3e6851447de89152a0dbadcd87e35fc947ca4bc886f1f87549ea106b097e2655136833d06dfb879a85732298860c149c5e5ff03bb2a95d9cd3deeb8ffdf951ea5f97e32c1ed75271d2ea58d158ae6d568bf197d69130977e330ebfef33f222bfd5b56bc6b0382dc99c4f0e42b0aa7a117b43f96d43f6e02dd" );
            iv_len = unhexify( iv_str, "523247d56cc67c752b20eab7a28f85fe" );
            add_len = unhexify( add_str, "11eb41aeae3611f0de77bfa1221ef5b7d254faf893dbdaead926a61605f8a86f20f1fb84e0c5acd195143bc5a4f297bf729129f898a2013175b3db7004115a6120134d8e354afe36699a6c6618d739c805b5b91739df67de7667729f1d6eae1a0609897999d474be4d8b826df901c6f39d522570d38d2d1aa828382932a177b1" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "39e7f32bb3e8436d97a1d86a22750768001fe3a805516d3f800352323afd221991105d12da69ce7430402fa7923958ad5ed85506b968c4dd89516d6e3d02e722db3954ce098ec3299ef4f2ed4a89f383408dceca9dabc6f8eefe5a1f80093961c29a94b222d1a04d2c1e453d2e02977f3dd77a4659e2bde2fdbba8e2829db4f1" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "506883db674fa0417e0832efc040227c" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810241024120_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "100bd2bf9c8b24cc2e8d57697cd131c846b55ad6ff0b214c0de14104b465b58b" );
            pt_len = unhexify( src_str, "81c3370da989f774c1962f60c57299747481bea0e6b91df846e6ef93cada977bc742ee33ce085ae33eb9f7393a0943b647205a7e1ffb2a6a803a1ce7a88902456d66612362962b97c7152b57f1d54de94a39f07c1a8098da4ea5e498d426b7036c642fbeebefda50b8c421a7a33b1a8499dc35011d80a51d34285824d6f01722" );
            iv_len = unhexify( iv_str, "363e8af6f38307ec126e466e7056cc45" );
            add_len = unhexify( add_str, "471f7e9a0b505b12996747ec9e32731f11911ee95d70795bbd1bba34cf782d4100ce30a85b23f9f817f30e8f314e1a23e101201c920ce12ce732cc3fe01c74a9ee8d3e1599aa22f2398c3265d4dbda626a8ff4262889009e087fbef6babe33d7300e5cfc4c0056f3562a913d2594fee8e44959cf728599a9d3e7ee4a9ecd6694" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "9494d01966ac887b8295bde61f0e7d006ea7b5c984a29cf5d849194f35d7b0f6ddb3bbd9646d7b9b961c515179901d2b04cb7cf7b6c8736d1d472ae8bb9a6dc9194b03b3f5373551a5ae0c0f023967669c873f0acfb02c0ae3a384e70f7a7ca05861f257f36a2ad5fbb591473dfc3ae1264dca0e889e0ddbf93dadf75db2059b" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "5c78d914cac78c514e275a244d0ea4" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810241024120_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "614dd1762deb5c726eadf0e6587f9f38fa63d16bca1926955404f1b9f83e241a" );
            pt_len = unhexify( src_str, "1ae828a1693d3c24651ab8ba59fb1185d08e6cc4a964f30dac59cd81ff4bdfce8023ab1b6dffb594a4250d25f611763efb4152cd35b937ca11373d237f1f8b3c0e21b942beb1f4ffe5014198c9ff59896ddfbb55e69963e3ef6b03d3fa134977870cd6f3ac10bbf59bdcc9f103cc2d58f294ef5f007a9f903c7bada08cb454e6" );
            iv_len = unhexify( iv_str, "10d079a86894b0c17bfcc8ffc4ecf7bc" );
            add_len = unhexify( add_str, "c4035f80b6d2ea288afd4ddaec1eb232b78be5a86583fa85f791d546102c97ace9716c2702483d762c8e4eda12f3dd10a9a49a2d72cd4694fa794477b54b4367be6b548675aee4c351e3f66c7e113aecfbcc57b8bbab4a039f28488237c75313e62612847b915ef9b582e146b2bfabbfce576a984f5ce4be0e6bff5480584fc3" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "bf5fb0445aab46aba504801d5356455f28c98f300670a731bdd0c901a1d5564aa31f5d467e5f80dadbfeca61d2bf72b570f3935ba04c45a2ff7994bac6cabf84db2a42cd5db2a4f160c97c76817cc5cb62d4006d895fcdb218c1464b5caaadbd1f61779938e9a84440615eae050cd6f1713cfbd695d78818b2af78157339e9d9" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "6d815ee12813875ce74e3aed3c7b73" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810241024120_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "12e97fafff7d397ea34efc0a8528afcd51c1b2ccda680ae9049edc8359b78ec0" );
            pt_len = unhexify( src_str, "9fbf0141cd50bd1b3ccaf137b808b698570642ab20c32120901622b34173d7ad119abca3c61bbf1e6dd5cb182a079f3e01b0e5263d984c6186f01792125dd6c47c30033008ca2e0377f990285094f652c55a348242dfaa59f76989fcf86033c8d9c0b2a526bf46cca207e055e1dbc7cf3d0b7a840c8fb5f85784c9e4563f71de" );
            iv_len = unhexify( iv_str, "8eb11abfe350c0d5a6b02477b44867e9" );
            add_len = unhexify( add_str, "0a830029d450e20aaef484d4abee9dadeabbd6feaf800b3a693b4746db059efb7d110405b45e45a9e5acf90957c154674dfb2c1cd787af371e01bafc4e8475d0268b969d25756a1121a519afa61f3d6ecded4e0640f0ddd471f5b8e82029fd2887df4e65af9580390b6924022e39acfede7530e5f0e54f0285ba565ff49af542" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "067cd6ff8461ac80217ef70a91dcf6edb2fbdd31856815cf356fffa63ba3f5cb293d7f1ed32ae40248693617f27839a34e871fdde635c04d1e66743f730a06e2be25cafe1d67d804879fe38e009268ec50a0294da445c795742ff1e924170e4c2e0e9ef3bdc26c251f5537218d295d93d57baccc4dee6185c235d7ec5c9926a6" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "931f44f10993c836e534a59c1aeb98" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810241024112_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "c732da000262de558bd3ea65e66e20e11605170c90b67708bda43f40abed74fe" );
            pt_len = unhexify( src_str, "7d6c981c30ef87a46f53aecb4c97124fb94b45057635d5bf1d4f3a3bdb534e9ab62b4a425de9dc52537575ed9ff406cfbf75403d3d9cdbd9fcd520d62065f81483427fa27964642cc1a07822da0f6234a689eb30e8425d7709abfd18666c76c963eecef20503ee77c96802c120abea1428cc64a08fc20860527854fecc571a6c" );
            iv_len = unhexify( iv_str, "523dd34ea263c31c2215053986626d02" );
            add_len = unhexify( add_str, "f170556ac5d38f0661bae33e0826356c8488218903eba1bfa49b16882537ef78283fd9351f37f44a7687049a608c3ddcc82817d4ba96a40d05807a38ee3f2d5cb8b1121db61318fe22bfd3afb319e84c4e2f94570a92433db29bd2193485449c719a2c6030696f53ac729df90678eb018783b25740d806d1ef6980e10d396595" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "3470d4544f7bfa3ac0627a56e66c56fa062188440834b9238bd20e89dfc701fe6cfe0bf4ea2387014bd83c63ab7c912e1c0dce7c2d92eaea155f886b574bc94a8f4f275dffe2d84173a05b99d8029c36dd3c35c12709d33f55c3bcd96e9a815f77a4fe8e50639d8f195a526486f1209d7bf7e86ac3dfc4a1d2cbddb6d330e5db" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "5924f3ceff0207fc8ba8179a9925" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810241024112_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "2684bccf2b845314a0c4b8b5a780f91aa7ed1177539122dc8717c14bb50e2dff" );
            pt_len = unhexify( src_str, "1a4174d4e18ae0b6434f35dcd9c86cf158c42ce00ceb12f4356ec118d659820518c326a1b2ab92279d949f74c45219c660cb84fb6b10b14d56a501173fd3b129ac89db0de22874d92bec724e94751f91a817a42a28e8e15672172c0b0db4ead46b14d4bc21ad8f5ba1f9e7e0fcc867700681349b8102a208d76ae4ef7df5b56e" );
            iv_len = unhexify( iv_str, "8433b59b41fe0cdc5b30e4e87c5028ec" );
            add_len = unhexify( add_str, "280026eeebf05e26e84955e4a36352d4f97f3193dce0795d526d05645bf5d2eec4b92ee8dce54d78fd3fc3e36bc79d5bf9ee3b2699310a75dbc5007bdacb4dc88d06515995f8f5b1aa90cb8fc036b763a5e819db70c091802fb7f24b9c2a68ff194032fffc4ef798936aabccbb43f22a2bbd7e1ab9d0434d443dac4929b84193" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "cc155e04472c0872d5ccf8910d34496f380954da7653a1e1d3c460fbbc791c9b82e35176e938b7e21eb4690ed9fca74ba45a03dac4abc4f625ffdfad02e1acccf18b5a1878f911fb6f6e09ce0d4c6a0bb87226e914879a1b3085c30e8328aa6e0d1c49c21b760b82e469981b40ea102f3998c81dd9799f484ab89b19396ab7e1" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "5a80008e6da40c71b316b84ae284" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810241024112_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "484a33ba0b97c2887a86a1476f274e236eb37a72e05f9e74348248877ea99e98" );
            pt_len = unhexify( src_str, "4d81cec14b398257a31ad1e3581c00d05e12b37b71260bdd95bc0b6981b614598ffbbb3ec4bb7deb5673a1020139877122f88504c9c53265706fe76623a9b488a3dfdd4cbc1b7b46c7fce9d7378e164964c0a377337a5c172e5e4de6206375164cd7beb0305d7a90f5c73e12f445326e1bc9ac5acd1bd4bcbe4662524891a2e9" );
            iv_len = unhexify( iv_str, "c3a5cc19aef6d64b656d66fad697b829" );
            add_len = unhexify( add_str, "30f276f96a50e17b452dcb5e1b4ab666dc7c4c72d0d9ab2abaf77eae2e3bab7dbe5ac005d7eac5480e1bae13646b59155528abdc148b3b71f06d017c4b12d64aa3990cc96941eaac14b60eb347e0be873de2b6fe2b86e2c2fc063b29511b70144ecd315b9491001b122701b9c8cc1d85427b6c60663ccd9d1fa84e1c2f609f36" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "579fd8fb50d795b5b208c2d5b0a8b1804f754a30a1003025301655aebcda2d2ff30d29a16d0fb17a28401127750fc87c9e3aa08540817228b049c387253ea2359035b8063ab4bf54504ca5ad93b54b8ac5bd0c1ef3c6769fb1ed239bb76f3e0bc51d356aa91b494d22749c8e4cdb1629e93f7c6e46ff9145916c1275669ae5ba" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "1c39aac1d5ffe7916a08ab2ce279" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810241024104_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "4a5f5321b515cfcde493148ee4c44c693b1979b3a3ba522a2a80e5d27c93fd1b" );
            pt_len = unhexify( src_str, "962b8504feb57ae73e93c2e8962c9562f409c908e51f9904df1623eaa0c6b998db6ee8919d805b6ffcc37da51300c1ae16bca21f8f6f63af989a813ae8fe28c3fb012f003dab7e71b08d757799208806062d62b4ac937712409f9fafff3e3579a4d92d4437a6f0b263e1da7e4651e0a521be5f6f49ff5a0778f07bd5d3dac696" );
            iv_len = unhexify( iv_str, "c2cb0166046bad0cf0a107af83921d7a" );
            add_len = unhexify( add_str, "e48abfb657ab33f58eeda8c58a20e7e299bc3e7481f704c326529408580f9a5130cf6f7368502d20b03ba6c3b8f6f28c076a3ef7b8e987750dc972be953e712483e6f328da57e4b5c501fa7c720593eb89ff9644fbdc45478f80ee89f096694dcb44a9b3a6aca0904d4aa4e475b4b24771df9fd6ef9557f4f5c842ac241b212f" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "11bd55d969603ff3d46355cb19c69557b99825a4c23eeafc8eed8422dab537c0fa9753191c49a6fd9e0d6760ed816a49e7f5704b5936a498544e2bbba7875c513c031f11527ca1b9b579960be6964fba9119dcece8205c174be07ebffada83375678de76fc012b0ee179787b4aa9fb6e2b459575260eb01f23786dc24d1d45ef" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "36853a029b5163ca76c72d4fec" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810241024104_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "c8f7b7e6295fc8e33740bf2807caeaf4b90817cc3ef3d9f38f704d9f6164e41d" );
            pt_len = unhexify( src_str, "4c26e489069b487ce9dc0e295d5e89760401185374041b0efca5bbf758e7d010ccbfe5999e2a817776aa8f49c1e5d43bcdade2989fe5be635dab54cb0e390a21b832b30f688857b9e09c346bcc5397e51cf71acbe1bfcaa1ecd7e87fe5dfde180d951922e60dd8203ff210c995eb54bb981f7e931f0b1f52dce0cf1b2eba503f" );
            iv_len = unhexify( iv_str, "903b2eeb9d0b3794acb7439d341cfe0d" );
            add_len = unhexify( add_str, "83e99497bfbe9393b065b0b18c13f99b67f1fdd724fd5d70cdccd2b8dd658499cb9f57e1a1fe39634ab0869182de085722a79eaabf057aac7b3f3230f51a2f9b48b49d592f02246dacbe915ff9d9a53f7e5332f7a9d89649050b075c07e5e74f281ca1a0dbe632c0aecf3b1911cd6ec4f8facc2777d0d14784bf5951a1c62c33" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "63e2941bf4a13374627be66bdd4e57119149f81f4c1a8a321d27a4a79e7d61e2dcec9d7b13fcccf12f5b059cc209f8414ae81966462a266e92b4b3c25198ee240e0bc6f6197df1e24e8d4379fcae89e6240a7f9c7bab886e79990b846e98e4bacb8b3b17422249943e9973de42da5e38e4eb52830b1facce766b3389a5312476" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "6e31c5db3146ae45ef5d50485e" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810241024104_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "dec062efc1bd2556b87a81143d025abbaa532c586d5ebb065859a2071f8f07e4" );
            pt_len = unhexify( src_str, "02191bcb060e61827dbddac6c2961dbab8812cdc2ac77bf0275628e8e36bae18ad4deb77b2682ade0aef76afd4592173ba29dae4d0735963c803856eaa6f60a6c21785358e87f3c4a91e321c59e04c150297de873679194ba5ca857f7d91ffc358e73810d555ebd4dbd1fe4fbc4ffa4ff38e4b41db9af0a84fe9828708631469" );
            iv_len = unhexify( iv_str, "19abd0361443c3ac2a46f2606eeb1a69" );
            add_len = unhexify( add_str, "c3785e7c0095726fd1f3ca842057b0ea2baf9c3fe1119c2147609158a2039f26cedf8a44e046955ba7e7cad9f48cb49274fc53b109d7897e080af252e7dc64807c276bcf668d2cd505c9ce8e584609d293ebd2a4515bfbaf78c413d6e29dc90974db38b564ffe9a40d3955dba9f19b6f39bf942669cf80e4676d6c10df566ca1" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "91a16c7fe029e3fddacf0809dde7d041c438977b89192e6fed7605d0133f3d9e810355d186432f6529bd2c4cb9dadb4fedf5128cb45e25a3a46bf74ed93f31349f64a69dbe86592d76e437947f1c1d7270d1cffe80afe10ae8523541961eacee1838c168a2ab76703ea4674a68a96b8a298a672ffc140e98e452d501fd57f000" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "5b4071a4be0543aaa59b56de35" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_2561281024102496_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "9b7b700d978e33ae9311b206347f488e2832fad5ce7e6026ad5e24fb47104fcb" );
            pt_len = unhexify( src_str, "37aef6e4200c6abc3d161daaf9dd6ede002ce8c63d9ed54e8ac56bdc8d36906bea663d2857d8d543166ba150827735ec78e37f92e682275e268d377b1880970df232162e55c9311882f889e7d183e5cf4972691c85f81c47e1224b9c97ee3963d75c6a032270ad6d713c999913f0b58a2d4f42b85a3b0b40541a31398cdfb4b0" );
            iv_len = unhexify( iv_str, "d0bbc284af767af9a31b863d66cb6138" );
            add_len = unhexify( add_str, "dfb87a65ab2d99d7d753042aa47448ad830e546d298d6ad52b85207bbb0cbe8cf3cdb12b3544f1fc228fdae04a241abf9e71de8ae14f2de2c261469c383c682e13582e07cddb1ed9bff1fd2aa0be7978096a914676dfbe7bec6edd927362f656ce1de86229bc511cfec4cda77a1e761e7ab8664e4df08cb820ebdb604c2cdbb0" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "dcd5575d94fffc647d4c081e3ce03928651419a32ada2af02de2f58d68fa98eb1fd5ef671875719a9c65b9ecc69513408a79a0a5d57cabd04f8e651f5b8fc1ff42ce58d8a212ac2bcb83c5c53c542c282553a62b4e3d7d4f049ab13172739a0f46e0a2fd9aec54eb0c84141c6b341783754372df69d39e48cc24eb3d9ddb21a9" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "4a7ac79db94b27469b92343a" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_2561281024102496_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "ce15e61edd9320ceacbf3984d87c707159caa738e7e76285be00b5a95954b523" );
            pt_len = unhexify( src_str, "8af4a7d92441ce931815fa4e24d69f66256fec7e62f79a029b684b5db304a46b2a3d3a7ee8d6b7ae38caa7de526d5c0f28dc65a0913a383b7ee1640cbe24997ba95b9b12fa1e9ce9f9100d883c16b6286dce17e381af15113f56197c97fe6b45be00a3df05045f476829d7b303211ac97cf989a18c16e27fbf23570d9d18f04b" );
            iv_len = unhexify( iv_str, "b1269c8495ea1469ff41d8154ae6765e" );
            add_len = unhexify( add_str, "0ad26a08a5cc2ec825347d7ffd5aac795eb68aa7e22970d991c863fa6d1fa720137aa5cde4e382625a0038e6ed72da3b5003c1b2a953c2b2138e0cf870cca4afb595c0451aa793fb0a2bc43834a0aca1e760590cca765ad672ead975993f82ae6765c5afbddc6062d7c4babebf650ab097db1a1d9a2a99e8fd2e0eb8a7b916f6" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "ad0ab4e77257866e4a57cf44fa4049428e56a6e8b8fd47b4cd00bfce84fa8f5a43f1df2061b0a37311b4a1436bad0d61d52ced5e262ed41a7eb125d61cec2e3fbaa95e533b43f318048096ebc8466f0cd609bb5e7c3fc6e5701aace546618a170f88c0b7ed76b63759ca4e4b931a86ac379dd12ad2cba7d47a19a3ae7c242fb0" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "fb1e988f9c97358a17e35e6f" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_2561281024102496_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "aef24b8205d4085d978505f04724293c2819ef9f3f03a6c758078690fc4bf7c8" );
            pt_len = unhexify( src_str, "db26453170db2f984312e0cf961d1a7df1154f0525c31f166be5c9f516736501f9f2dd8096a69b6441888ce27aaceacb0b365a38e4e01e2e34027c023206e814f22d46fd2fa69f87509ddced4b8852a76b2532b92f069b8c922ac13b2b7f19cb7c524657a4ee6e989cf2598bef674aa31576776853fb7f9a2704d6b3ee7fbcbb" );
            iv_len = unhexify( iv_str, "81456baa337c3dfd162d9c5f72a2e216" );
            add_len = unhexify( add_str, "484a5f4772643cf74ccdced0e5d80862f9300f26ae3139968649d3d7bb761b313f2ba63798b2040d397c3d1569285fee8498fd9254851c15b98af5bd351fa72e7d574c62ede0d728e1279e8b4e4784fd63ea7851e99d1d2356bcbf868528f8d0a90fc3b884ece631648d916ec97abadca1b0dd7670e6ad42245021570582ec7c" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "da95c61cd2bb88fea78c059c254d2b949d4fc291c73ac178ace44c1e6a339f64931c857d3a7cb276a04993620adb6918dfd3f9083edad384a8e6c1d4799d526a1c969d8deb0e2667d6d06f559baf914b49fc463244528aa6522d19699065438d939521d7d7bb149835298f2054bcaae6d786f6dde133b640697a3d37c697579a" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "bc1c1cbcad2e1a66ace079a2" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_2561281024102464_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "9685aea9aaebbd691e679779034729306d5887bee4c1f90f6ee3a397a0ff3ece" );
            pt_len = unhexify( src_str, "ae3b2fa1e209f72c167eb16bc15b7669b87d4ab516e428157810b87a83e90d56e267bd4996522b5b22c2a349d3765ca27ea27057dd71f7c18ddd053033bd780b6cb689f48c383e9c717b9b265cb9e32c70c4a7d8fb933e986d996b5ad914cd645b74c47ac3a0de952ee3fc73ada83d896da7ca0b2a0b10e4f701fa13cba9ec50" );
            iv_len = unhexify( iv_str, "b1bc140531ae8c69e2ffc784e0988038" );
            add_len = unhexify( add_str, "294ff858fa6efc82ca3be4d05332bbb951a71a7ddfa4b78472e1582b445312eec11793d8d6e1e858d9cb078b5fc9083ac8a3e3bd82964cb07c08450567922299f68fd47663c7a77c29f2b5347f229301433d5a75263158a0d80095859e7e45476b99b23412046bfbe4eafff9f7820ba49919d2c987cf00c286c784e7669d8fe8" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "6575128b576e68f7b3709e325b3d616783b42ff7f7631eb62b90cb0c8a86bd324756f43af53c33cbdaf9cf64ea94cf1b7fab5003f00c1d07f3fc8eb1931d759f9c43477ba22311a111488092c42b7786facf42b861a824cd1bcdc603a77d11253f15206a929a3e16e8737d080b8e5f0da8896226989a9964d72e491187250472" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "f78c4dd37c06b197" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_2561281024102464_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "3adf0da24394a98c7beae01d28f261a9cbd887aeeecc0c29e84540264d5a6bad" );
            pt_len = unhexify( src_str, "8cf023d717b0f82f2b81750b53fb665c1c90f4740af4a3534b36b847df33ba5eec19eb24ead70a4b613a82572878216181d59b0c4c4df99be08d021cf182724d8ff5ec4e85884d0f69c16238fbbdbc5529ffcc4e418405e4e95139f79d3115a1ac56820cd39fc413ab72f7d447f947cb0541fc2be261f1246c0a786199013b22" );
            iv_len = unhexify( iv_str, "ad41288817577316df2d881ac93fcdef" );
            add_len = unhexify( add_str, "ad33ce922372fbe3531c0dece69f85f18eb1bbfb09a178403832308de0e54b1010db2636c4b7d9caa478138f61db5149c9fd7f3b45b7a1876729fe67622a37f0b322ef9cf6043b301a5d4c81e6f347d22bd3e40722059d3be945845c6b0629fbcfcaf885c7f393aa81f242c48c61a439574761ef6b671972cac664403250750e" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "9d465e9c4228323946b1261892243d8455edb9eb8633d026d4033fa3965d20730979ba6952c0f6f2c5768f03c19256b64bc759d2e7b92424bbc668308504ba34384c2bb37baaf91a3a4f0952a050a3d69853141b49e86eda3bf0c4db4ebcd1c41e7f13eca20bf574a47ec45b8c98def17c0741805bf8f37923ba2b5221428578" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "507618cec6d03964" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_2561281024102464_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "9ef64b4132db54668568e2ae66ab61f62a820c7002a67a7e42006280a373feba" );
            pt_len = unhexify( src_str, "4b96dce753273188c4cca3386a7415d5d9263757376e1f32797df47992e92e1bc0ab0833363b3acffde22602d4e47307bc8f252944414a15e1398693fd3b8bf4d8101cdcf70ce2c9de8cb7f5bb17cd83f09b1bc78ba07c34b9214e250c5940e9794199cb392309027d5ab4f32b51c533db6732024bd412f2cb0c5178d5296aa5" );
            iv_len = unhexify( iv_str, "07a86dbe2cce040eccdad79b3d211ecc" );
            add_len = unhexify( add_str, "af7a75748ee293015b600ca82ccc7718f4ecc20c3a2357ee02fb726330a0d79ca8bb97979bc0c89f4c60d7154f8bd29ba6ec5f2f4be286ea8a258cf6bd39b4f42d6db8e70c99ec3af26bb4d8003dc6fd0fdfbbc620d511d4d5f09ddf975a1663ac2979ae0978b0bc1e7bfcd660ae4ac7f1a8f6d8ee35752ed59a604f07dfda53" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "e3e862146b6fb48b01ababc462dd560298eea7bfe5f3248e28a908d1de08c7e91fcf63922c394e7a51b64f4382225093e78598c050e588ff4ad38f3e83dc07b77ce569c6ab8f8a9cb0056b3155aa1503cebeb64c86d6d9cdbb178ea9a01a8ba33a1c48beb92ee4cf60e7dedf986019e19089cd186c98c229b0ff42c9e1aca571" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "8614c216055c0660" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_2561281024102432_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "f14ac79f35bc5a685433eea5bb7fd69fc959aabda24cbd8b7795fb2e41f90ab0" );
            pt_len = unhexify( src_str, "8a20da14819079960b77ed5e548d0aa0bdcffb752817c1abe4195e612cfbb58c8e5a8af69f75bad10ee8afdf0b0d5c46c4dc11c32bff16d5e7e82e77fd80e475c6a5a0be36718af232697ab22314306b8ee32484b3461da657710c06170e80a6a8844f898c2be29366c8430f2392d100ffd419603cbce406dc7315577e6e9ee2" );
            iv_len = unhexify( iv_str, "353e1d08edce44c966430513cb7a0383" );
            add_len = unhexify( add_str, "cb1dde4ff5a6867038c170192fc2d292f5bb349d5b9a903cf3d88c09ce78fb1f4a776ff7588a25abb5e5f6a44791d7296afef3f32ed31db1def37dd25be0570a204955121f9c65b79a3ea88fc452dbcb82719243c11bc27e3408adf802b6e8b4e701ee4e9dfd140cb3277bf605bd5fb757d2325f7805fc6f0d1ea5a6207fac5f" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "49b5e4ea0421034c074cde67dd39a0310c3f31e8138672ba2ecc0777be542f1c6529836d5206b79dac83d96aab56787a35c584b31228f007f11630328c3f40a57be37487689ee5babb576e7d14ff0f1f1ba6e4be11637352a4336327681058b99df2e44f9772de4e0e456d2e34dec5eeb335b238e862841d166e0612cc0f18f3" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "88aed643" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_2561281024102432_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "b55ac909e73989e310ae37d13c54bbd5a126f419a3b01a2ad8961d89bd247f81" );
            pt_len = unhexify( src_str, "8a663e8b21a027c4a9545d145d42d9c67b4fcd5d0e39aa68822aedbd609e2c681f60e6315035321de739858b2b082bc05551fe9b8456c2e89c6151282c6068b915eae5762e4d6d765d667de58a315e061b3d60035ada50f59258eb6e2a1cd6b52eea7eb9d404fd96e71f19feff65b74a4b4f07061adf7c1b0e54e2ece7a2cd49" );
            iv_len = unhexify( iv_str, "9328abab0d3f63c75ddafd8559d96b4f" );
            add_len = unhexify( add_str, "cbae20aa1996abb62471aac91cd78080953fbe3b165d4c9435832ef1106e7e3424db8850f44a431c289ab4f2bbbea9e5c0c7aaf2e8de69c0ced176283662cadd280d8fda0c859551f0f90893ca57695c95803a1546826922ac78703d7ccae285b7ccd4bbab551756cccc6869dcf34b6af8d8b80c25c6fb1d2caa7f28161fb854" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "457e13ff4eeaaae75d14bbf1bff91706c3168b9b146aed29dbe31b12ad90c1c158833be95701229ac6e4a13997e0a2d961d4a0021c4d8920ec54a9a935e5ea73b17e8fa60559df76bd07d966dfa7d86d1a77a313228b2ae7f66b5b696726c02af2c808bf75e0b9591a220e762f57c680ca68f20b2b5413b07731bbd49de039bf" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "5de0434a" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_2561281024102432_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "1477e189fb3546efac5cc144f25e132ffd0081be76e912e25cbce7ad63f1c2c4" );
            pt_len = unhexify( src_str, "7bd3ea956f4b938ebe83ef9a75ddbda16717e924dd4e45202560bf5f0cffbffcdd23be3ae08ff30503d698ed08568ff6b3f6b9fdc9ea79c8e53a838cc8566a8b52ce7c21b2b067e778925a066c970a6c37b8a6cfc53145f24bf698c352078a7f0409b53196e00c619237454c190b970842bb6629c0def7f166d19565127cbce0" );
            iv_len = unhexify( iv_str, "c109f35893aff139db8ed51c85fee237" );
            add_len = unhexify( add_str, "8f7f9f71a4b2bb0aaf55fced4eb43c57415526162070919b5f8c08904942181820d5847dfd54d9ba707c5e893a888d5a38d0130f7f52c1f638b0119cf7bc5f2b68f51ff5168802e561dff2cf9c5310011c809eba002b2fa348718e8a5cb732056273cc7d01cce5f5837ab0b09b6c4c5321a7f30a3a3cd21f29da79fce3f3728b" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "7841e3d78746f07e5614233df7175931e3c257e09ebd7b78545fae484d835ffe3db3825d3aa1e5cc1541fe6cac90769dc5aaeded0c148b5b4f397990eb34b39ee7881804e5a66ccc8d4afe907948780c4e646cc26479e1da874394cb3537a8f303e0aa13bd3cc36f6cc40438bcd41ef8b6a1cdee425175dcd17ee62611d09b02" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "cb13ce59" ) == 0 );
            }
        }
        FCT_TEST_END();

    }
    FCT_SUITE_END();

#endif /* POLARSSL_GCM_C */

}
FCT_END();
